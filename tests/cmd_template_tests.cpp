// Command templates: the templateable option contract and the `template`
// subcommand (spec: command-templates).
#include "app/prelude.h"
#include "cmd/cmd.h"
#include "cmd/config_builder.h"
#include "cmd/config_store.h"
#include "cmd/template_command.h"
#include "cmd/template_store.h"
#include "core/path_utf8.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ConfigEnvScope {
  explicit ConfigEnvScope(fs::path const& configPath)
    : guard_("ENCRO_CONFIG", pathutf8::fromPath(configPath)) { }

  testutils::ScopedEnvVar guard_;
};

auto realTemplateTable() -> configstore::KeyTable const& {
  static auto const table = testutils::parseArgs({"encro"}).templateTable;
  return table;
}

auto templateLongNames(configstore::KeyTable const& table) -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  for (auto const& def: table.keys) { names.emplace_back(def.longName); }
  return names;
}

}  // namespace

TEST_CASE(
  "every templateable boolean flag is registered with its negation form",
  "[cmd][template]"
) {
  auto result = CmdParseResult{};
  auto const tree = buildAppTree(result, "", false);
  REQUIRE_FALSE(result.error.has_value());

  auto const& table = result.templateTable;
  REQUIRE_FALSE(table.keys.empty());

  auto booleans = std::vector<std::string>{};
  for (auto const& def: table.keys) {
    if (def.kind != configstore::JsonKind::Boolean) { continue; }
    booleans.emplace_back(def.key);

    auto* opt = tree.app->get_option_no_throw(def.longName);
    INFO("flag --" << def.key);
    REQUIRE(opt != nullptr);
    // A negation form is a second long name on the same option, which is what
    // the help renderer collapses into --[no-]name.
    auto const& longNames = opt->get_lnames();
    CHECK(std::ranges::find(longNames, std::format("no-{}", def.key)) != longNames.end());
  }

  // The fifteen flags a template can store, each with a negation form; a new
  // flag without one fails here instead of leaving a stored value
  // unoverridable.
  CHECK(booleans.size() == 15);
}

TEST_CASE(
  "the occurrence-counting and path options are not templateable",
  "[cmd][template]"
) {
  auto result = CmdParseResult{};
  auto const tree = buildAppTree(result, "", false);
  REQUIRE_FALSE(result.error.has_value());

  auto const names = templateLongNames(result.templateTable);
  for (auto const& path: {"--input", "--inputs", "--state-file", "--verbose", "--help"}) {
    INFO(path);
    CHECK(std::ranges::find(names, std::string{path}) == names.end());
  }

  // -v keeps its suppressed count semantics even though it is a flag.
  auto* verbose = tree.app->get_option_no_throw("--verbose");
  REQUIRE(verbose != nullptr);
  CHECK(
    std::ranges::find(verbose->get_lnames(), std::string{"no-verbose"})
    == verbose->get_lnames().end()
  );
}

TEST_CASE("template names normalize and reject escapes", "[cmd][template][store]") {
  using templatestore::normalizeName;

  CHECK(normalizeName("anime") == "anime");
  CHECK(normalizeName("Anime") == "anime");  // lookups are case-insensitive
  CHECK(normalizeName("my.task_1-x") == "my.task_1-x");

  CHECK(normalizeName("").empty());
  CHECK(normalizeName("../evil").empty());
  CHECK(normalizeName("a b").empty());
  CHECK(normalizeName(".hidden").empty());
  CHECK(normalizeName("templates.schema").empty());  // reserved for the schema
}

TEST_CASE(
  "the templates directory sits beside the config file",
  "[cmd][template][store]"
) {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  CHECK(templatestore::resolveTemplatesDir() == temp.path / "templates");
}

TEST_CASE("listing templates skips the schema file", "[cmd][template][store]") {
  auto const temp = TempDir{};
  auto const dir = temp.path / "templates";
  testutils::writeTextFile(dir / "anime.json", "{}");
  testutils::writeTextFile(dir / "webp.json", "{}");
  testutils::writeTextFile(dir / "templates.schema.json", "{}");
  testutils::writeTextFile(dir / "notes.txt", "not a template");

  CHECK(templatestore::list(dir) == std::vector<std::string>{"anime", "webp"});
  CHECK(templatestore::list(temp.path / "absent").empty());
}

TEST_CASE(
  "template lookup is case-insensitive and refuses the reserved name",
  "[cmd][template][store]"
) {
  auto const temp = TempDir{};
  auto const dir = temp.path / "templates";
  testutils::writeTextFile(dir / "anime.json", "{}");
  testutils::writeTextFile(dir / "templates.schema.json", "{}");

  CHECK(templatestore::find(dir, "anime") == dir / "anime.json");
  CHECK(templatestore::find(dir, "Anime") == dir / "anime.json");
  CHECK_FALSE(templatestore::find(dir, "templates.schema").has_value());
  CHECK_FALSE(templatestore::find(dir, "nosuch").has_value());
  CHECK_FALSE(templatestore::find(temp.path / "absent", "anime").has_value());
}

TEST_CASE(
  "a template round-trips through the writer with native types",
  "[cmd][template][store]"
) {
  auto const temp = TempDir{};
  auto const path = temp.path / "templates" / "anime.json";
  auto const& table = realTemplateTable();

  auto const values = std::map<std::string, std::string>{
    {"crf", "24"},
    {"keep", "true"},
    {"output", "E:\\out with space"},
  };
  REQUIRE_FALSE(templatestore::save(path, values, {}, table).has_value());

  auto const text = testutils::readTextFile(path);
  CHECK(text.starts_with("{\n    \"$schema\": \"./templates.schema.json\""));
  CHECK(text.find("\"crf\": 24") != std::string::npos);           // native number
  CHECK(text.find("\"keep\": true") != std::string::npos);        // native boolean
  CHECK(text.find("E:\\\\out with space") != std::string::npos);  // escaped path
  CHECK(text.find("\"keep\"") < text.find("\"crf\""));            // canonical order

  auto const loaded = templatestore::load(path, table);
  REQUIRE_FALSE(loaded.error.has_value());
  CHECK(loaded.values == values);
  CHECK(loaded.unknownKeys.empty());
}

TEST_CASE("a hand-edited unknown key survives a rewrite", "[cmd][template][store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "templates" / "anime.json";
  auto const& table = realTemplateTable();

  testutils::writeTextFile(
    path,
    R"({"$schema":"./templates.schema.json","crf":24,"model":"x"})"
  );

  auto const loaded = templatestore::load(path, table);
  REQUIRE_FALSE(loaded.error.has_value());
  CHECK(loaded.values.at("crf") == "24");
  CHECK(loaded.unknownKeys == std::vector<std::string>{"model"});
  // $schema is reserved: neither a value nor an unknown key.
  CHECK(loaded.values.find("$schema") == loaded.values.end());
  CHECK(loaded.unknownValues.find("$schema") == loaded.unknownValues.end());

  REQUIRE_FALSE(
    templatestore::save(path, loaded.values, loaded.unknownValues, table).has_value()
  );
  auto const text = testutils::readTextFile(path);
  CHECK(text.find("\"model\": \"x\"") != std::string::npos);
  CHECK(text.find("\"$schema\": \"./templates.schema.json\"") != std::string::npos);
}

TEST_CASE("a malformed template names the file", "[cmd][template][store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "templates" / "anime.json";
  testutils::writeTextFile(path, "{ not json !!!");

  auto const loaded = templatestore::load(path, realTemplateTable());
  REQUIRE(loaded.error.has_value());
  CHECK(loaded.error->find("template file") != std::string::npos);
  CHECK(loaded.error->find(pathutf8::fromPath(path)) != std::string::npos);
}

TEST_CASE(
  "the generated schema describes the templateable keys",
  "[cmd][template][store]"
) {
  auto const document = templatestore::schema(realTemplateTable());

  // It parses, and it is closed to unknown keys.
  auto const parsed = boost::json::parse(document);
  REQUIRE(parsed.is_object());
  CHECK_FALSE(parsed.as_object().at("additionalProperties").as_bool());
  auto const& properties = parsed.as_object().at("properties").as_object();
  CHECK(properties.contains("$schema"));

  auto const& crf = properties.at("crf").as_object();
  CHECK(crf.at("type").as_string() == "integer");
  CHECK(crf.at("minimum").to_number<double>() == 0.0);
  CHECK(crf.at("maximum").to_number<double>() == 51.0);
  CHECK(crf.at("default").to_number<double>() == 28.0);
  CHECK_FALSE(crf.at("description").as_string().empty());

  auto const& preset = properties.at("preset").as_object();
  CHECK(preset.at("type").as_string() == "string");
  CHECK(preset.at("enum").as_array().size() == 8);

  auto const& keep = properties.at("keep").as_object();
  CHECK(keep.at("type").as_string() == "boolean");
  CHECK_FALSE(keep.at("default").as_bool());  // a flag's built-in default
  CHECK_FALSE(keep.contains("minimum"));
}

TEST_CASE(
  "saving refreshes the schema file beside the templates",
  "[cmd][template][store]"
) {
  auto const temp = TempDir{};
  auto const dir = temp.path / "templates";
  auto const& table = realTemplateTable();

  REQUIRE_FALSE(templatestore::writeSchema(dir, table).has_value());
  auto const path = dir / "templates.schema.json";
  REQUIRE(fs::exists(path));
  auto const before = testutils::readTextFile(path);
  CHECK(before.find("\"crf\"") != std::string::npos);

  // A hand-damaged schema is rewritten complete on the next save.
  testutils::writeTextFile(path, "{\n  \"properties\": {\n  }\n}\n");
  REQUIRE_FALSE(templatestore::writeSchema(dir, table).has_value());
  CHECK(testutils::readTextFile(path) == before);
  CHECK(templatestore::schema(table) == before);
}

// ── `template` subcommand actions ──────────────────────────────────────

namespace {

// Runs one `encro template ...` command against a TempDir-backed store and
// returns its exit code plus whatever it wrote to stdout.
struct TemplateRun {
  int exitCode = -1;
  std::string stdoutText;
};

auto runTemplate(TempDir const& temp, std::vector<std::string> const& args)
  -> TemplateRun {
  auto run = TemplateRun{};
  auto const out = temp.path / "stdout.txt";
  {
    auto const capture = testutils::StdoutCapture{out};
    run.exitCode = cmd::runTemplateCommand(testutils::parseArgs(args));
  }
  if (fs::exists(out)) { run.stdoutText = testutils::readTextFile(out); }
  return run;
}

}  // namespace

TEST_CASE("bare template prints the subcommand help", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  auto const run = runTemplate(temp, {"encro", "template"});
  CHECK(run.exitCode == 0);
  CHECK(run.stdoutText.find("encro template list") != std::string::npos);
  CHECK(run.stdoutText.find("save <name> [options]") != std::string::npos);
}

TEST_CASE(
  "an unknown action and a wrong argument count fail natively",
  "[cmd][template][actions]"
) {
  auto const unknown = testutils::parseArgs({"encro", "template", "nope"});
  REQUIRE(unknown.error.has_value());
  CHECK(unknown.error->find("nope") != std::string::npos);

  auto const tooFew = testutils::parseArgs({"encro", "template", "show"});
  REQUIRE(tooFew.error.has_value());
  CHECK(tooFew.error->find("1 argument expected") != std::string::npos);

  auto const extra = testutils::parseArgs({"encro", "template", "unset", "anime"});
  REQUIRE(extra.error.has_value());
  CHECK(extra.error->find("2 arguments expected") != std::string::npos);
}

TEST_CASE("save creates a template and confirms once", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  auto const path = temp.path / "templates" / "anime.json";

  auto const run = runTemplate(
    temp,
    {"encro", "template", "save", "anime", "--crf", "24", "-t", "picture", "--no-pack"}
  );
  CHECK(run.exitCode == 0);
  REQUIRE(fs::exists(path));
  CHECK(run.stdoutText.find("saved template anime") != std::string::npos);
  CHECK(run.stdoutText.find("anime.json") != std::string::npos);
  CHECK(std::ranges::count(run.stdoutText, '\n') == 1);  // one line only

  auto const text = testutils::readTextFile(path);
  CHECK(text.find("\"type\": \"picture\"") != std::string::npos);
  CHECK(text.find("\"crf\": 24") != std::string::npos);
  // A negation form captures the false value.
  CHECK(text.find("\"pack\": false") != std::string::npos);
  // The schema lands beside the template.
  CHECK(fs::exists(temp.path / "templates" / "templates.schema.json"));
}

TEST_CASE("a second save merges instead of replacing", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  auto const path = temp.path / "templates" / "anime.json";

  REQUIRE(
    runTemplate(
      temp,
      {"encro", "template", "save", "anime", "--crf", "24", "--preset", "p5", "--keep"}
    )
      .exitCode
    == 0
  );
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "22"}).exitCode == 0
  );

  auto const text = testutils::readTextFile(path);
  CHECK(text.find("\"crf\": 22") != std::string::npos);
  CHECK(text.find("\"crf\": 24") == std::string::npos);
  CHECK(text.find("\"preset\": \"p5\"") != std::string::npos);
  CHECK(text.find("\"keep\": true") != std::string::npos);
}

TEST_CASE("save refuses an input path and writes nothing", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  auto const refused =
    testutils::parseArgs({"encro", "template", "save", "anime", "-i", "C:\\footage"});
  REQUIRE(refused.error.has_value());
  CHECK(refused.error->find("--input") != std::string::npos);
  CHECK(refused.error->find("template") != std::string::npos);
  CHECK_FALSE(fs::exists(temp.path / "templates" / "anime.json"));
}

TEST_CASE("save refuses a positional input path", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  // The word cannot reach the capture walk: the subcommand's own positional
  // slots take it, so the arity check rejects the line before anything is
  // written.
  auto const refused =
    testutils::parseArgs({"encro", "template", "save", "anime", "C:\\footage"});
  REQUIRE(refused.error.has_value());
  CHECK(refused.error->find("1 argument expected") != std::string::npos);
  CHECK_FALSE(fs::exists(temp.path / "templates" / "anime.json"));
}

TEST_CASE("save with nothing to store is an error", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  auto const empty = testutils::parseArgs({"encro", "template", "save", "anime"});
  REQUIRE(empty.error.has_value());
  CHECK(empty.error->find("nothing to save") != std::string::npos);
  CHECK_FALSE(fs::exists(temp.path / "templates" / "anime.json"));
}

TEST_CASE(
  "save rejects a name that could escape the directory",
  "[cmd][template][actions]"
) {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  auto const run =
    runTemplate(temp, {"encro", "template", "save", "../evil", "--crf", "24"});
  CHECK(run.exitCode == 1);
  CHECK_FALSE(fs::exists(temp.path / "evil.json"));
}

TEST_CASE(
  "list shows each template with the options it stores",
  "[cmd][template][actions]"
) {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24", "--keep"})
      .exitCode
    == 0
  );
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "webp", "-t", "picture", "--no-pack"})
      .exitCode
    == 0
  );

  auto const run = runTemplate(temp, {"encro", "template", "list"});
  CHECK(run.exitCode == 0);
  CHECK(run.stdoutText.find("anime") != std::string::npos);
  CHECK(run.stdoutText.find("--crf 24") != std::string::npos);
  CHECK(run.stdoutText.find("--keep") != std::string::npos);
  CHECK(run.stdoutText.find("webp") != std::string::npos);
  CHECK(run.stdoutText.find("--no-pack") != std::string::npos);
  CHECK(run.stdoutText.find("templates.schema") == std::string::npos);
}

TEST_CASE(
  "list on an empty store suggests how to create one",
  "[cmd][template][actions]"
) {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  auto const run = runTemplate(temp, {"encro", "template", "list"});
  CHECK(run.exitCode == 0);
  CHECK(run.stdoutText.find("no templates yet") != std::string::npos);
  CHECK_FALSE(fs::exists(temp.path / "templates"));
}

TEST_CASE(
  "show prints the canonical file and path prints the directory",
  "[cmd][template][actions]"
) {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24"}).exitCode == 0
  );

  auto const shown = runTemplate(temp, {"encro", "template", "show", "anime"});
  CHECK(shown.exitCode == 0);
  CHECK(
    shown.stdoutText == testutils::readTextFile(temp.path / "templates" / "anime.json")
  );

  auto const pathRun = runTemplate(temp, {"encro", "template", "path"});
  CHECK(pathRun.exitCode == 0);
  CHECK(pathRun.stdoutText.find("templates") != std::string::npos);
}

TEST_CASE("unset drops one key and leaves the rest", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  auto const path = temp.path / "templates" / "anime.json";
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24", "--keep"})
      .exitCode
    == 0
  );

  CHECK(runTemplate(temp, {"encro", "template", "unset", "anime", "crf"}).exitCode == 0);
  auto const text = testutils::readTextFile(path);
  CHECK(text.find("\"crf\"") == std::string::npos);
  CHECK(text.find("\"keep\": true") != std::string::npos);

  // An absent key is a no-op; an unknown key is an error naming it.
  auto const before = testutils::readTextFile(path);
  CHECK(
    runTemplate(temp, {"encro", "template", "unset", "anime", "preset"}).exitCode == 0
  );
  CHECK(testutils::readTextFile(path) == before);

  auto const unknown =
    runTemplate(temp, {"encro", "template", "unset", "anime", "nosuchkey"});
  CHECK(unknown.exitCode == 1);
  CHECK(testutils::readTextFile(path) == before);
}

TEST_CASE("unset falls back to the stored config value", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  testutils::writeTextFile(temp.path / "config.json", R"({"crf": 23})");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24"}).exitCode == 0
  );

  auto const withTemplate = testutils::parseArgs({"encro", "-T", "anime"});
  REQUIRE_FALSE(withTemplate.error.has_value());
  CHECK(withTemplate.crf == 24);

  REQUIRE(
    runTemplate(temp, {"encro", "template", "unset", "anime", "crf"}).exitCode == 0
  );
  auto const afterUnset = testutils::parseArgs({"encro", "-T", "anime"});
  REQUIRE_FALSE(afterUnset.error.has_value());
  CHECK(afterUnset.crf == 23);
}

TEST_CASE(
  "delete removes the template and rejects an unknown name",
  "[cmd][template][actions]"
) {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24"}).exitCode == 0
  );

  CHECK(runTemplate(temp, {"encro", "template", "delete", "anime"}).exitCode == 0);
  CHECK_FALSE(fs::exists(temp.path / "templates" / "anime.json"));
  CHECK(
    runTemplate(temp, {"encro", "template", "list"}).stdoutText.find("anime")
    == std::string::npos
  );

  auto const unknown = runTemplate(temp, {"encro", "template", "delete", "nosuch"});
  CHECK(unknown.exitCode == 1);
}

TEST_CASE(
  "the template subcommand help shows only its own options",
  "[cmd][template][actions]"
) {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  // The subcommand allows fallthrough so `encro template save anime --crf 24`
  // parses; CLI11 then appends the parent's options to get_options(), which
  // must not reach the rendered help.
  auto const run = runTemplate(temp, {"encro", "template", "-h"});
  CHECK(run.exitCode == 0);
  CHECK(run.stdoutText.find("--help") != std::string::npos);
  CHECK(run.stdoutText.find("--version") == std::string::npos);
  CHECK(run.stdoutText.find("--verbose") == std::string::npos);
}

TEST_CASE("the schema action prints what save wrote", "[cmd][template][actions]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24"}).exitCode == 0
  );

  auto const printed = runTemplate(temp, {"encro", "template", "schema"});
  CHECK(printed.exitCode == 0);
  auto const schemaPath = temp.path / "templates" / "templates.schema.json";
  CHECK(printed.stdoutText == testutils::readTextFile(schemaPath));

  // A hand-damaged schema is rebuilt complete by the next save.
  testutils::writeTextFile(schemaPath, "{\n  \"properties\": {\n  }\n}\n");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "22"}).exitCode == 0
  );
  CHECK(testutils::readTextFile(schemaPath) == printed.stdoutText);
}

// ── -T selection, the default layer and diagnostics ────────────────────

namespace {

// initStartup builds argv itself, so the verbose-attribution tests get their
// own runner (parseArgs returns without setting logging up).
auto initStartupFor(std::vector<std::string> const& args) -> prelude::StartupContext {
  auto storage = args;
  auto argv = std::vector<char*>{};
  for (auto& arg: storage) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);
  return prelude::initStartup(static_cast<int>(argv.size() - 1), argv.data(), "");
}

}  // namespace

TEST_CASE("a plain run reads no template", "[cmd][template][select]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  auto const result = testutils::parseArgs({"encro", "--crf", "24"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK_FALSE(result.selectedTemplate.has_value());
  CHECK_FALSE(result.appliedTemplate.has_value());
  CHECK(result.crf == 24);
}

TEST_CASE("a value-less -T lists the templates", "[cmd][template][select]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24"}).exitCode == 0
  );

  auto const result = testutils::parseArgs({"encro", "-T"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK(result.templates);
  CHECK(result.templateVerb == "list");

  auto const run = runTemplate(temp, {"encro", "-T"});
  CHECK(run.exitCode == 0);
  CHECK(run.stdoutText.find("anime") != std::string::npos);
}

TEST_CASE("two template selections fail during parsing", "[cmd][template][select]") {
  auto const result = testutils::parseArgs({"encro", "-T", "anime", "-T", "webp"});
  REQUIRE(result.error.has_value());
  CHECK(result.error->find("template") != std::string::npos);
}

TEST_CASE("-T has no effect on the store subcommands", "[cmd][template][select]") {
  for (
    auto const& args:
    {std::vector<std::string>{"encro", "-T", "anime", "config", "list"},
     std::vector<std::string>{"encro", "-T", "anime", "completion", "bash"},
     std::vector<std::string>{"encro", "-T", "anime", "template", "list"}}
  ) {
    INFO(args[3]);
    auto const result = testutils::parseArgs(args);
    REQUIRE(result.error.has_value());
    CHECK(result.error->find("--template has no effect") != std::string::npos);
  }
}

TEST_CASE("the template beats a stored config value", "[cmd][template][select]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  testutils::writeTextFile(temp.path / "config.json", R"({"crf": 23, "preset": "p5"})");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24"}).exitCode == 0
  );

  SECTION("no command-line value") {
    auto const result = testutils::parseArgs({"encro", "-T", "anime"});
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.crf == 24);                         // template wins
    CHECK(result.nvencPreset == std::string{"p5"});  // config value survives
    CHECK(result.appliedTemplate->name == std::string{"anime"});
  }

  SECTION("an explicit value wins over both") {
    auto const result = testutils::parseArgs({"encro", "-T", "anime", "--crf", "30"});
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.crf == 30);
    CHECK(result.nvencPreset == std::string{"p5"});
  }
}

TEST_CASE("a stored boolean does not trip an exclusion rule", "[cmd][template][select]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--dry-run"}).exitCode == 0
  );

  // --dry-run excludes --crf as parsed arguments; as a template default it
  // must not, because the command line expresses crf explicitly.
  auto const result = testutils::parseArgs({"encro", "-T", "anime", "--crf", "20"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK(result.dryRun);
  CHECK(result.crf == 20);
}

TEST_CASE("a stored boolean is cleared by its negation form", "[cmd][template][select]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--keep"}).exitCode == 0
  );

  auto const stored = testutils::parseArgs({"encro", "-T", "anime"});
  REQUIRE_FALSE(stored.error.has_value());
  CHECK(stored.keep);

  auto const cleared = testutils::parseArgs({"encro", "-T", "anime", "--no-keep"});
  REQUIRE_FALSE(cleared.error.has_value());
  CHECK_FALSE(cleared.keep);
}

TEST_CASE(
  "a stored output alias resolves against the run's inputs",
  "[cmd][template][select]"
) {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "webp", "-o", "+/webp"}).exitCode == 0
  );

  auto const inputPath = temp.path / "run" / "clip.mp4";
  fs::create_directories(inputPath.parent_path());
  testutils::writeTextFile(inputPath, "x");

  auto const result =
    testutils::parseArgs({"encro", "-T", "webp", pathutf8::fromPath(inputPath)});
  REQUIRE_FALSE(result.error.has_value());

  auto const configRes = cmd::buildConfig(result);
  REQUIRE(configRes);
  REQUIRE(configRes->outputPath.has_value());
  CHECK(configRes->outputPath.value() == inputPath.parent_path() / "webp");
}

TEST_CASE("an unknown template fails naming it", "[cmd][template][select]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");

  auto const result = testutils::parseArgs({"encro", "-T", "anme", "--crf", "24"});
  REQUIRE(result.error.has_value());
  CHECK(result.error->find("anme") != std::string::npos);
  CHECK(result.error->find("encro template list") != std::string::npos);
}

TEST_CASE("a malformed template fails before processing", "[cmd][template][select]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  auto const path = temp.path / "templates" / "anime.json";
  testutils::writeTextFile(path, "{ not json !!!");

  auto const result = testutils::parseArgs({"encro", "-T", "anime", "--crf", "24"});
  REQUIRE(result.error.has_value());
  CHECK(result.error->find("template file") != std::string::npos);
  CHECK(result.error->find(pathutf8::fromPath(path)) != std::string::npos);
}

TEST_CASE("an unknown key warns and the rest applies", "[cmd][template][select]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  auto const path = temp.path / "templates" / "anime.json";
  testutils::writeTextFile(
    path,
    R"({"$schema":"./templates.schema.json","crf":24,"model":"x"})"
  );

  auto const stderrPath = temp.path / "stderr.txt";
  {
    auto const capture = testutils::StderrCapture{stderrPath};
    auto const result = testutils::parseArgs({"encro", "-T", "anime"});
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.crf == 24);
  }

  auto const warnings = testutils::readTextFile(stderrPath);
  CHECK(warnings.find("model") != std::string::npos);
  CHECK(warnings.find("anime") != std::string::npos);
  CHECK(warnings.find("$schema") == std::string::npos);
}

TEST_CASE("a verbose run names the applied template", "[cmd][template][verbose]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "config.json");
  REQUIRE(
    runTemplate(temp, {"encro", "template", "save", "anime", "--crf", "24"}).exitCode == 0
  );

  auto const verbosePath = temp.path / "stderr-verbose.txt";
  {
    auto const capture = testutils::StderrCapture{verbosePath};
    (void)initStartupFor({"encro", "-v", "-T", "anime", "--crf", "30"});
    testutils::shutdownLogging();
  }
  auto const verbose = testutils::readTextFile(verbosePath);
  CHECK(verbose.find("Applied template 'anime'") != std::string::npos);
  CHECK(verbose.find("anime.json") != std::string::npos);

  // Without -v the run stays silent about the template.
  auto const quietPath = temp.path / "stderr-quiet.txt";
  {
    auto const capture = testutils::StderrCapture{quietPath};
    (void)initStartupFor({"encro", "-T", "anime", "--crf", "30"});
    testutils::shutdownLogging();
  }
  CHECK(testutils::readTextFile(quietPath).find("Applied template") == std::string::npos);
}

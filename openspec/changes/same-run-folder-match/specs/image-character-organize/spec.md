## ADDED Requirements

### Requirement: Same-run folder references

Folder matching SHALL use this run's own character-tag assignment as a second source of references, so that the first run over a gallery can file clusters into the folders that same run created. The images this run assigned to one character folder — by character tag, or because an existing folder owns that tag — SHALL form a reference for that folder; `mixed/` and `uncategorized/` SHALL NOT become references, because a group of images that is not a single character cannot teach one. A reference's profile is the mean of its members' normalized identity features and the mean of their unit identity-tag vectors, and it is compared under the same rules the teaching requirement applies to existing folders — the same weighted combination, the same separately calibrated default when a side carries no identity-tag evidence, and the same threshold — so that a cluster reaching that threshold against it is filed into that folder's name. A routing-derived reference and an on-disk folder of the same name SHALL be one reference whose membership is the union of both sources. An on-disk folder SHALL take precedence where the two disagree: images assigned a character tag that an existing folder owns are filed into the owning folder's current name, and a routing-derived reference SHALL NOT cause an existing output folder to be renamed or deleted, nor change the contents it already holds. Routing-derived references SHALL be deterministic — they depend only on the run's analyses and routing decisions, not on filesystem iteration order. A run whose routing assigns no image to a character folder SHALL behave exactly as it did before this requirement existed.

#### Scenario: A first run files a cluster into the folder its own routing created

- **WHEN** a run scans a gallery with no output folders yet, one image's character tag `hatsune_miku` files it under `hatsune_miku`, and a cluster's profile reaches the identity similarity threshold against the profile of the images filed there
- **THEN** that cluster's images are copied into `hatsune_miku` in the same run instead of receiving an `unknown_` name

#### Scenario: A renamed folder keeps owning its character tag

- **WHEN** a previous run produced `hatsune_miku/`, the user renamed it to `初音ミク`, and the current run assigns images to `hatsune_miku` by character tag
- **THEN** those images and any cluster matching their profile are filed into `初音ミク`, and no `hatsune_miku` folder appears

#### Scenario: A routing-derived reference captures nothing below the threshold

- **WHEN** a cluster's profile stays below the identity similarity threshold against every reference, including the references this run's routing produced
- **THEN** the cluster keeps an `unknown_` name instead of being filed into a folder

#### Scenario: A run that routes no character behaves as before

- **WHEN** routing assigns no image to any character folder (everything is `mixed/` or unassigned)
- **THEN** clustering, cluster naming and folder matching produce the same result as the release before this requirement existed

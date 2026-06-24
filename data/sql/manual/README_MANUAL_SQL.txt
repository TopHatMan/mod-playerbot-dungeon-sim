If AzerothCore auto-updater does not create the tables, run these manually against the correct databases:

1) Against your WORLD database (example: azc_world_ashbringer):
   data/sql/db-world/updates/2026_06_24_99_playerbot_dungeon_sim_force_world_install.sql

2) Against your CHARACTERS database (example: azc_characters_ashbringer):
   data/sql/db-characters/updates/2026_06_24_99_playerbot_dungeon_sim_force_characters_install.sql

Do not run the character install against world. The runtime tables such as playerbot_dungeon_run live in CHARACTERS.

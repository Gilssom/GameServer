USE GameDB;

-- Expected schema constraints used by the login and character systems.
SELECT
    EXISTS (
        SELECT 1
        FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'Account'
          AND COLUMN_NAME = 'account_id'
          AND COLUMN_TYPE = 'bigint unsigned'
          AND IS_NULLABLE = 'NO'
          AND EXTRA LIKE '%auto_increment%'
    ) AS account_id_definition_valid,
    EXISTS (
        SELECT 1
        FROM information_schema.TABLE_CONSTRAINTS tc
        JOIN information_schema.KEY_COLUMN_USAGE kcu
          ON kcu.CONSTRAINT_SCHEMA = tc.CONSTRAINT_SCHEMA
         AND kcu.TABLE_NAME = tc.TABLE_NAME
         AND kcu.CONSTRAINT_NAME = tc.CONSTRAINT_NAME
        WHERE tc.CONSTRAINT_SCHEMA = DATABASE()
          AND tc.TABLE_NAME = 'Account'
          AND tc.CONSTRAINT_TYPE = 'PRIMARY KEY'
          AND kcu.COLUMN_NAME = 'account_id'
    ) AS account_id_primary_key_exists,
    EXISTS (
        SELECT 1
        FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'Account'
          AND COLUMN_NAME = 'account_name'
          AND NON_UNIQUE = 0
    ) AS account_name_unique_exists,
    EXISTS (
        SELECT 1
        FROM information_schema.KEY_COLUMN_USAGE
        WHERE CONSTRAINT_SCHEMA = DATABASE()
          AND TABLE_NAME = 'CharacterData'
          AND COLUMN_NAME = 'account_id'
          AND REFERENCED_TABLE_NAME = 'Account'
          AND REFERENCED_COLUMN_NAME = 'account_id'
    ) AS character_account_foreign_key_exists,
    EXISTS (
        SELECT 1
        FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'CharacterData'
          AND COLUMN_NAME = 'character_name'
          AND NON_UNIQUE = 0
    ) AS character_name_unique_exists;

-- Every count must be zero. Non-zero values indicate existing data corruption.
SELECT
    (
        SELECT COUNT(*)
        FROM (
            SELECT account_name
            FROM Account
            GROUP BY account_name
            HAVING COUNT(*) > 1
        ) duplicate_accounts
    ) AS duplicate_account_name_groups,
    (
        SELECT COUNT(*)
        FROM CharacterData character_data
        LEFT JOIN Account account
          ON account.account_id = character_data.account_id
        WHERE account.account_id IS NULL
    ) AS orphan_character_count,
    (
        SELECT COUNT(*)
        FROM (
            SELECT character_name
            FROM CharacterData
            GROUP BY character_name
            HAVING COUNT(*) > 1
        ) duplicate_characters
    ) AS duplicate_character_name_groups;

SHOW CREATE TABLE Account;
SHOW CREATE TABLE CharacterData;

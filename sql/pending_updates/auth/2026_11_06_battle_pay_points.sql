-- Shop points live on the account. 10000 stored points = 1.00 of shop currency.
-- Default balance is 100.00 so the pending catalog can be purchased on a test realm.

ALTER TABLE `account`
  ADD COLUMN `battle_pay_points` bigint unsigned NOT NULL DEFAULT 1000000 AFTER `hasBoost`;

CREATE TABLE IF NOT EXISTS `battle_pay_log` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `accountId` int unsigned NOT NULL,
  `characterGuid` int unsigned NOT NULL DEFAULT 0,
  `realm` int unsigned NOT NULL DEFAULT 0,
  `productId` int unsigned NOT NULL,
  `itemId` int unsigned NOT NULL DEFAULT 0,
  `price` bigint unsigned NOT NULL DEFAULT 0,
  `time` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`accountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

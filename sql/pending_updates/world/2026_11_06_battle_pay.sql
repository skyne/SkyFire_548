-- Battle Pay shop catalog. Product 83 is the existing character boost (hasBoost), not a second boost implementation.
-- Prices are display units. The server scales them by 10000 when talking to the client.
-- An empty catalog is valid: the server still answers the product list.

CREATE TABLE IF NOT EXISTS `battle_pay_group` (
  `id` int unsigned NOT NULL,
  `idx` int unsigned NOT NULL DEFAULT 0,
  `name` varchar(32) NOT NULL,
  `icon` int unsigned NOT NULL DEFAULT 0,
  `type` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `battle_pay_group_locale` (
  `ID` int unsigned NOT NULL,
  `Locale` varchar(4) NOT NULL,
  `Name` varchar(32) NOT NULL DEFAULT '',
  PRIMARY KEY (`ID`, `Locale`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `battle_pay_product` (
  `id` int unsigned NOT NULL,
  `title` varchar(55) NOT NULL,
  `description` varchar(500) NOT NULL DEFAULT '',
  `icon` int unsigned NOT NULL DEFAULT 0,
  `price` int unsigned NOT NULL DEFAULT 0,
  `discount` int unsigned NOT NULL DEFAULT 0,
  `displayId` int unsigned NOT NULL DEFAULT 0,
  `type` tinyint unsigned NOT NULL DEFAULT 0,
  `choiceType` tinyint unsigned NOT NULL DEFAULT 1,
  `flags` int unsigned NOT NULL DEFAULT 0,
  `flagsInfo` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `battle_pay_product_locale` (
  `ID` int unsigned NOT NULL,
  `Locale` varchar(4) NOT NULL,
  `Title` varchar(55) NOT NULL DEFAULT '',
  `Description` varchar(500) NOT NULL DEFAULT '',
  PRIMARY KEY (`ID`, `Locale`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `battle_pay_product_items` (
  `id` int unsigned NOT NULL,
  `itemId` int unsigned NOT NULL DEFAULT 0,
  `count` int unsigned NOT NULL DEFAULT 1,
  `productId` int unsigned NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `battle_pay_entry` (
  `id` int unsigned NOT NULL,
  `productId` int unsigned NOT NULL,
  `groupId` int unsigned NOT NULL,
  `idx` int NOT NULL DEFAULT 0,
  `title` varchar(55) NOT NULL,
  `description` varchar(500) NOT NULL DEFAULT '',
  `icon` int unsigned NOT NULL DEFAULT 0,
  `displayId` int unsigned NOT NULL DEFAULT 0,
  `banner` tinyint unsigned NOT NULL DEFAULT 0,
  `flags` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `battle_pay_entry_locale` (
  `ID` int unsigned NOT NULL,
  `Locale` varchar(4) NOT NULL,
  `Title` varchar(55) NOT NULL DEFAULT '',
  `Description` varchar(500) NOT NULL DEFAULT '',
  PRIMARY KEY (`ID`, `Locale`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `battle_pay_group` (`id`, `idx`, `name`, `icon`, `type`) VALUES
(1, 1, 'Featured', 939376, 1),
(2, 2, 'Bags', 940857, 0),
(3, 3, 'Services', 939382, 0)
ON DUPLICATE KEY UPDATE `idx` = VALUES(`idx`), `name` = VALUES(`name`), `icon` = VALUES(`icon`), `type` = VALUES(`type`);

INSERT INTO `battle_pay_product` (`id`, `title`, `description`, `icon`, `price`, `discount`, `displayId`, `type`, `choiceType`, `flags`, `flagsInfo`) VALUES
(1, 'Embersilk Bag', '22-slot bag.', 348521, 5, 0, 0, 0, 1, 47, 0),
(83, 'Level 90 Character Boost', 'Grants the character boost used by the character screen.', 614740, 25, 0, 0, 2, 1, 975, 0)
ON DUPLICATE KEY UPDATE `title` = VALUES(`title`), `description` = VALUES(`description`), `icon` = VALUES(`icon`), `price` = VALUES(`price`), `discount` = VALUES(`discount`), `displayId` = VALUES(`displayId`), `type` = VALUES(`type`), `choiceType` = VALUES(`choiceType`), `flags` = VALUES(`flags`), `flagsInfo` = VALUES(`flagsInfo`);

INSERT INTO `battle_pay_product_items` (`id`, `itemId`, `count`, `productId`) VALUES
(1, 54443, 1, 1)
ON DUPLICATE KEY UPDATE `itemId` = VALUES(`itemId`), `count` = VALUES(`count`), `productId` = VALUES(`productId`);

INSERT INTO `battle_pay_entry` (`id`, `productId`, `groupId`, `idx`, `title`, `description`, `icon`, `displayId`, `banner`, `flags`) VALUES
(1, 83, 1, 0, 'Level 90 Character Boost', 'Grants the character boost used by the character screen.', 236544, 0, 2, 0),
(2, 83, 3, 0, 'Level 90 Character Boost', 'Grants the character boost used by the character screen.', 236544, 0, 2, 0),
(3, 1, 2, 0, 'Embersilk Bag', '22-slot bag.', 348521, 0, 0, 0)
ON DUPLICATE KEY UPDATE `productId` = VALUES(`productId`), `groupId` = VALUES(`groupId`), `idx` = VALUES(`idx`), `title` = VALUES(`title`), `description` = VALUES(`description`), `icon` = VALUES(`icon`), `displayId` = VALUES(`displayId`), `banner` = VALUES(`banner`), `flags` = VALUES(`flags`);

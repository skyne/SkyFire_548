-- Map-ID simulation cluster registry (one owner per mapId per realm).
-- Used by world nodes and clustergateway. Empty until Cluster.Enabled is set.

CREATE TABLE IF NOT EXISTS `cluster_node` (
  `node_id` int unsigned NOT NULL,
  `realm_id` int unsigned NOT NULL,
  `host` varchar(255) NOT NULL DEFAULT '127.0.0.1',
  `link_port` smallint unsigned NOT NULL DEFAULT '8086',
  `available_maps` text NOT NULL,
  `state` tinyint unsigned NOT NULL DEFAULT '1' COMMENT '1=starting, 2=up, 3=draining, 4=down',
  `heartbeat` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`realm_id`,`node_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COMMENT='Cluster world nodes';

CREATE TABLE IF NOT EXISTS `cluster_map_assignment` (
  `realm_id` int unsigned NOT NULL,
  `map_id` int unsigned NOT NULL,
  `node_id` int unsigned NOT NULL,
  `claimed_at` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`realm_id`,`map_id`),
  KEY `idx_cluster_map_node` (`realm_id`,`node_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COMMENT='Exactly one live owner per mapId';

CREATE TABLE IF NOT EXISTS `cluster_account_route` (
  `account_id` int unsigned NOT NULL,
  `character_guid` int unsigned NOT NULL DEFAULT '0',
  `realm_id` int unsigned NOT NULL,
  `map_id` int unsigned NOT NULL DEFAULT '0',
  `node_id` int unsigned NOT NULL,
  `updated_at` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`character_guid`,`realm_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COMMENT='Cold login and gateway reconnect routing';

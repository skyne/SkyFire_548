-- Continent layers: several nodes may each simulate the same map.
-- layer_id equals node_id. Instance, raid, and battleground maps stay one owner.

ALTER TABLE `cluster_map_assignment`
  DROP PRIMARY KEY,
  ADD COLUMN `layer_id` int unsigned NOT NULL DEFAULT 1 AFTER `map_id`,
  ADD COLUMN `population` int unsigned NOT NULL DEFAULT 0 AFTER `node_id`,
  ADD PRIMARY KEY (`realm_id`, `map_id`, `layer_id`),
  COMMENT = 'Continent layers (one row per node); one owner for instanceable maps';

UPDATE `cluster_map_assignment` SET `layer_id` = `node_id`;

ALTER TABLE `cluster_account_route`
  ADD COLUMN `layer_id` int unsigned NOT NULL DEFAULT 1 AFTER `node_id`,
  ADD COLUMN `desired_node_id` int unsigned NOT NULL DEFAULT 0 AFTER `layer_id`,
  COMMENT = 'Cold login and gateway reconnect routing, including continent layer';

UPDATE `cluster_account_route` SET `layer_id` = `node_id`;

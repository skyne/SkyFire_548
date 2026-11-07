-- Scenario point-of-interest blobs queried by CMSG_QUERY_SCENARIO_POI.
-- Rows are optional; an empty table still lets the scenario UI load.

CREATE TABLE IF NOT EXISTS `scenario_poi` (
  `CriteriaTreeID` int(11) NOT NULL DEFAULT '0',
  `BlobIndex` int(11) NOT NULL DEFAULT '0',
  `Idx1` int(11) NOT NULL DEFAULT '0',
  `MapID` int(11) NOT NULL DEFAULT '0',
  `WorldMapAreaId` int(11) NOT NULL DEFAULT '0',
  `Floor` int(11) NOT NULL DEFAULT '0',
  `Priority` int(11) NOT NULL DEFAULT '0',
  `Flags` int(11) NOT NULL DEFAULT '0',
  `WorldEffectID` int(11) NOT NULL DEFAULT '0',
  `PlayerConditionID` int(11) NOT NULL DEFAULT '0',
  PRIMARY KEY (`CriteriaTreeID`, `Idx1`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `scenario_poi_points` (
  `CriteriaTreeID` int(11) NOT NULL DEFAULT '0',
  `Idx1` int(11) NOT NULL DEFAULT '0',
  `Idx2` int(11) NOT NULL DEFAULT '0',
  `X` int(11) NOT NULL DEFAULT '0',
  `Y` int(11) NOT NULL DEFAULT '0',
  PRIMARY KEY (`CriteriaTreeID`, `Idx1`, `Idx2`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

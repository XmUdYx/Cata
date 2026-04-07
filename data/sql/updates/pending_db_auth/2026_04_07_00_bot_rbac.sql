--
-- Bot session GM commands: .bot spawn / despawn / move / teleport
-- RBAC permissions 1436–1439
-- Linked to Role 196 (Administrator Commands) and Role 197 (Gamemaster Commands)
--

DELETE FROM `rbac_permissions` WHERE `id` IN (1436, 1437, 1438, 1439);
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1436, 'Command: bot spawn'),
(1437, 'Command: bot despawn'),
(1438, 'Command: bot move'),
(1439, 'Command: bot teleport');

DELETE FROM `rbac_linked_permissions` WHERE `id` IN (196, 197) AND `linkedId` IN (1436, 1437, 1438, 1439);
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(196, 1436),
(196, 1437),
(196, 1438),
(196, 1439),
(197, 1436),
(197, 1437),
(197, 1438),
(197, 1439);

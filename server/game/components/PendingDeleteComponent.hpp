#pragma once

namespace voxelmmo {

/**
 * @brief Marks an entity for deferred deletion.
 *
 * Set by ChunkMembershipSystem::markForDeletion() when an entity should be removed.
 * Processed by ChunkMembershipSystem::updateEntitiesChunks() to remove the entity
 * from its chunk and queue for destruction after serialization.
 *
 * Entities marked with this component are NOT destroyed immediately - they remain
 * in the registry so their DELETE delta can be serialized to watching clients.
 * Actual destruction happens after serialization via destroyPendingDeletions().
 */
struct PendingDeleteComponent {};

} // namespace voxelmmo

#pragma once
#include "component_manager.h"
#include "entity_manager.h"
#include "system.h"
#include "system_manager.h"
#include <memory>
#include <type_traits>
namespace ECS {
class Coordinator {
public:
  void Init();
  Entity CreateEntity();
  Signature GetEntitySignature(Entity entity);
  void DestroyEntity(Entity entity);
  void FreeSystems();
  // generic component methods
  template <typename T> void RegisterComponent() {
    mComponentManager->RegisterComponent<T>();
  }
  template <typename T> void AddComponent(Entity entity, T component) {
    mComponentManager->AddComponent<T>(entity, component);

    auto signature = mEntityManager->GetSignature(entity);
    signature.set(mComponentManager->GetComponentType<T>(), true);
    mEntityManager->SetSignature(entity, signature);

    mSystemManager->EntitySignatureChanged(entity, signature);
  }
  template <typename T> void RemoveComponent(Entity entity) {
    mComponentManager->RemoveComponent<T>(entity);

    auto signature = mEntityManager->GetSignature(entity);
    signature.set(mComponentManager->GetComponentType<T>(), true);
    mEntityManager->SetSignature(entity, signature);

    mSystemManager->EntitySignatureChanged(entity, signature);
  }
  template <typename T> T &GetComponent(Entity entity) {
    return mComponentManager->GetComponent<T>(entity);
  }
  template <typename T> ComponentType GetComponentType() {
    return mComponentManager->GetComponentType<T>();
  }
  // generic system methods
  template <typename T> std::shared_ptr<T> RegisterSystem() {
    // static asserts are always good
    static_assert(std::is_base_of<System, T>::value,
                  "T must derive from ECS::System");
    return mSystemManager->RegisterSystem<T>();
  }
  template <typename T> void SetSystemSignature(Signature signature) {
    mSystemManager->SetSignature<T>(signature);
  }

private:
  std::unique_ptr<ComponentManager> mComponentManager;
  std::unique_ptr<EntityManager> mEntityManager;
  std::unique_ptr<SystemManager> mSystemManager;
};
} // namespace ECS

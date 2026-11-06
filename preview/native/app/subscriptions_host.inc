// Existing serialized Host owns the service and its joined concrete fetch.
  std::unique_ptr<search::SavedSubscriptionService> subscriptions;
  bool subscriptionsNormal=false;
  std::optional<ui::SubscriptionCommand> subscriptionCommand;
  std::shared_ptr<std::atomic_bool> subscriptionCommitted;
  quint64 subscriptionCatalogGeneration=0,subscriptionLastRequest=0;
  std::unique_ptr<SavedSubscriptionAcquisition> subscriptionAcquisition;
  std::optional<ui::SubscriptionCommand> acquisitionCommand;
  std::shared_ptr<std::atomic_bool> acquisitionCommitted;

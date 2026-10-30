// Inside libtorrent::aux. Private opt-in; no Qt object or user callback crosses
// this executor. No capability is exposed by status or saved settings.
#if TORRENT_USE_I2P
std::uint32_t session_impl::biglybtc_configure_private_i2p_owner(BiglybtcPrivateI2pOwnerPolicyV1 const& policy)
{
  using result=BiglybtcPrivateI2pOwnerResultV1;
  TORRENT_ASSERT(is_single_thread());
  if(!i2p_owner_context::valid(policy))return std::uint32_t(result::InvalidArgument);
  if(m_abort||m_i2p_conn.owned_bridge()||m_i2p_conn.is_open()||num_torrents()!=0
    ||!m_settings.get_str(settings_pack::i2p_hostname).empty()
    ||m_settings.get_int(settings_pack::i2p_port)!=0)return std::uint32_t(result::Busy);
  for(int key:{settings_pack::enable_incoming_tcp,settings_pack::enable_outgoing_tcp,
    settings_pack::enable_incoming_utp,settings_pack::enable_outgoing_utp,
    settings_pack::enable_dht,settings_pack::enable_lsd,settings_pack::enable_upnp,
    settings_pack::enable_natpmp,settings_pack::allow_i2p_mixed})
    if(m_settings.get_bool(key))return std::uint32_t(result::InvalidArgument);
  if(m_settings.get_int(settings_pack::proxy_type)!=settings_pack::none
    ||!m_settings.get_str(settings_pack::listen_interfaces).empty())return std::uint32_t(result::InvalidArgument);
  m_i2p_conn.set_owned_bridge(std::make_shared<i2p_owner_context>(policy));
  return std::uint32_t(result::Accepted);
}
BiglybtcPrivateI2pOwnerStatusV1 session_impl::biglybtc_private_i2p_owner_status()const
{
  TORRENT_ASSERT(is_single_thread());BiglybtcPrivateI2pOwnerStatusV1 status;
  if(auto owner=m_i2p_conn.owned_bridge()){
    status.generation=owner->policy.generation;status.admission=owner->policy.admission;
    status.public_destination_hash=owner->policy.public_destination_hash;
    status.bridge_port=owner->policy.bridge_port;status.phase=owner->current()?1U:2U;
  }return status;
}
std::uint32_t session_impl::biglybtc_withdraw_private_i2p_owner(std::uint64_t generation)
{
  TORRENT_ASSERT(is_single_thread());using result=BiglybtcPrivateI2pOwnerResultV1;
  auto owner=m_i2p_conn.owned_bridge();
  if(!owner||owner->policy.generation!=generation)return std::uint32_t(result::Stale);
  owner->withdraw();abort();return std::uint32_t(result::Accepted);
}
#endif

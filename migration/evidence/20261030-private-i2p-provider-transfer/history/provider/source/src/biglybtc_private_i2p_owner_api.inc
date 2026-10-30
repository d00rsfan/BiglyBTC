#if TORRENT_USE_I2P
std::uint32_t session_handle::biglybtc_configure_private_i2p_owner(BiglybtcPrivateI2pOwnerPolicyV1 const& policy)const
{return sync_call_ret<std::uint32_t>(&session_impl::biglybtc_configure_private_i2p_owner,std::cref(policy));}
BiglybtcPrivateI2pOwnerStatusV1 session_handle::biglybtc_private_i2p_owner_status()const
{return sync_call_ret<BiglybtcPrivateI2pOwnerStatusV1>(&session_impl::biglybtc_private_i2p_owner_status);}
std::uint32_t session_handle::biglybtc_withdraw_private_i2p_owner(std::uint64_t generation)const
{return sync_call_ret<std::uint32_t>(&session_impl::biglybtc_withdraw_private_i2p_owner,generation);}
#endif
extern "C" TORRENT_EXPORT unsigned biglybtc_private_i2p_owner_abi_v1()noexcept
{
#if TORRENT_USE_I2P
  return BiglybtcPrivateI2pOwnerAbiV1;
#else
  return 0;
#endif
}
extern "C" TORRENT_EXPORT std::uint32_t biglybtc_configure_private_i2p_owner_v1(session_handle const* handle,BiglybtcPrivateI2pOwnerPolicyV1 const* policy)noexcept
{
  using result=BiglybtcPrivateI2pOwnerResultV1;
  if(!handle||!policy)return std::uint32_t(result::InvalidArgument);
  if(!handle->is_valid())return std::uint32_t(result::InvalidHandle);
#if TORRENT_USE_I2P
  try{return handle->biglybtc_configure_private_i2p_owner(*policy);}catch(...){return std::uint32_t(result::Failed);}
#else
  return std::uint32_t(result::Failed);
#endif
}
extern "C" TORRENT_EXPORT std::uint32_t biglybtc_read_private_i2p_owner_v1(session_handle const* handle,BiglybtcPrivateI2pOwnerStatusV1* output)noexcept
{
  using result=BiglybtcPrivateI2pOwnerResultV1;
  if(!output){return std::uint32_t(result::InvalidArgument);}
  *output={};
  if(!handle||!handle->is_valid())return std::uint32_t(result::InvalidHandle);
#if TORRENT_USE_I2P
  try{*output=handle->biglybtc_private_i2p_owner_status();return std::uint32_t(result::Accepted);}catch(...){return std::uint32_t(result::Failed);}
#else
  return std::uint32_t(result::Failed);
#endif
}
extern "C" TORRENT_EXPORT std::uint32_t biglybtc_withdraw_private_i2p_owner_v1(session_handle const* handle,std::uint64_t generation)noexcept
{
  using result=BiglybtcPrivateI2pOwnerResultV1;
  if(!handle||!generation)return std::uint32_t(result::InvalidArgument);
  if(!handle->is_valid())return std::uint32_t(result::InvalidHandle);
#if TORRENT_USE_I2P
  try{return handle->biglybtc_withdraw_private_i2p_owner(generation);}catch(...){return std::uint32_t(result::Failed);}
#else
  return std::uint32_t(result::Failed);
#endif
}

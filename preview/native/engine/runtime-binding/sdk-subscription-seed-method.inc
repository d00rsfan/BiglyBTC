
		// The owner supplies the hash from a verified, signed BiglyBT subscription.
		// Ordinary metainfo parsing and all v2 identities retain their normal hashes.
		bool use_biglybt_subscription_hash(sha1_hash const& verified_hash)
		{
			if (!is_valid() || priv() || !m_info_hash.has_v1() || m_info_hash.has_v2()
				|| num_files() != 1 || piece_length() != 256 * 1024 || verified_hash.is_all_zeros())
				return false;
			error_code ec;
			auto const contents = bdecode(info_section(), ec);
			if (ec || contents.type() != bdecode_node::dict_t) return false;
			auto const stored_hash = contents.dict_find_string_value("hash-override");
			if (stored_hash.size() != 20 || !std::equal(stored_hash.begin(), stored_hash.end(), verified_hash.data()))
				return false;
			m_info_hash.v1 = verified_hash;
			return true;
		}

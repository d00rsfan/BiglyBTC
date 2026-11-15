# Original cached-value selection prerequisite

Native `95e04a1213d9ae809e91e63c0c6f46843f792c29`, tree `0815497cd2457096bd46de2a8b0093d22f77d039`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The bounded allocation-free selection projection matches19 actual calls to
`DHTDBImpl.republishCachedMappings`, including local/default/explicit replication
mixes, both diversification types, recent/due/future store times, signed-long
subtraction wrap, one final forward before dropping a nonclosest mapping, and
failed anti-spoof lookup. The actual original DB/Mapping/Value objects perform
selection and relay; only router/contact/control effects are controlled by test
proxies. Reflection sets the original mapping's cached diversification state.
The original helper runs on JDK17 with an explicit generated config directory.

Native checks include five boundary groups for age equality, strict greater,
clock rollback,256-value capacity and invalid inputs. Original relay captures
preserve version/flags/lifetime/replication and use the actual local sender.
Explicit-replication-only mappings remain survey-only even when survey is
otherwise disabled; they are never silently forwarded through the default path.

Focused build and original/native comparison passed. No full application rebuild
or unchanged green regressions were needed for this unconnected pure helper.
This is a prerequisite milestone only. The DB candidate API, current-content
receipts, normal-owner bounded forwarding, survey and key-block maintenance are
still in progress. No live profile, public endpoint or daily image was used.

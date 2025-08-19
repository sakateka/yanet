#include <cstdint>
#include <cstring>
#include <iostream>

// Mock DPDK and common types for size calculation
struct rte_ether_addr
{
	uint8_t addr_bytes[6];
};

namespace common
{
namespace globalBase
{
struct tFlow
{
	uint32_t data;
};
}
namespace fwstate
{
enum class owner_e : uint8_t
{
	unknown = 0
};
}
}

// Basic type definitions
using tInterfaceId = uint16_t;
using tCounterId = uint32_t;

// IPv4 and IPv6 address structures
struct ipv4_address_t
{
	uint32_t address;
};

struct ipv6_address_t
{
	union
	{
		uint8_t bytes[16];
		struct
		{
			uint8_t nap[12];
			ipv4_address_t mapped_ipv4_address;
		};
	};
};

// Value types from the codebase

// 1. Neighbor value
struct neighbor_value
{
	rte_ether_addr ether_address;
	uint16_t flags;
	uint32_t last_update_timestamp;
};

// 2. Firewall state value
enum class fw_state_type : uint8_t
{
	tcp = 6,
	udp = 17,
};

struct fw_udp_state_value_t
{};

struct fw_tcp_state_value_t
{
	uint8_t src_flags : 4;
	uint8_t dst_flags : 4;
};

struct fw_state_value_t
{
	fw_state_type type;
	common::fwstate::owner_e owner;
	union
	{
		fw_udp_state_value_t udp;
		fw_tcp_state_value_t tcp;
	};
	uint32_t last_seen;
	uint32_t state_timeout;
	common::globalBase::tFlow flow;
	uint32_t last_sync;
	uint32_t packets_since_last_sync;
	uint64_t packets_backward;
	uint64_t packets_forward;
	uint8_t acl_id;
};

// 3. NAT64 stateful LAN value
struct nat64stateful_lan_value
{
	ipv4_address_t ipv4_source;
	uint16_t port_source;
	uint16_t timestamp_last_packet;
	uint32_t flags;
};

// 4. NAT64 stateful WAN value
struct nat64stateful_wan_value
{
	union
	{
		ipv6_address_t ipv6_source;
		struct
		{
			uint8_t nap[12];
			uint16_t port_destination;
			uint16_t timestamp_last_packet;
		};
	};
	ipv6_address_t ipv6_destination;
	uint32_t flags;
};

// 5. Balancer state value
struct balancer_state_value_t
{
	uint32_t real_unordered_id;
	uint32_t timestamp_create;
	uint32_t timestamp_last_packet;
	uint32_t timestamp_gc;
	uint32_t state_timeout;
};

// 6. ACL transport key (used as value in hashtable_mod_id32)
using tAclGroupId = uint32_t;
struct transport_key_t
{
	tAclGroupId network_id : 32;
	tAclGroupId protocol : 16;
	tAclGroupId group1 : 16;
	tAclGroupId group2 : 16;
	tAclGroupId group3 : 8;
	tAclGroupId network_flags : 8;
};

// 7. ACL total key (used as value in hashtable_mod_id32)
struct total_key_t
{
	tAclGroupId acl_id;
	tAclGroupId transport_id;
};

// 8. IPv6 address (used as value in network destination hashtables)
// Already defined above

// 9. Actions (used as value in ACL values array)
namespace common
{
struct Actions
{
	uint32_t action_data[4]; // Approximate size
};
}

int main()
{
	std::cout << "YANET Hashtable Value Sizes Analysis\n";
	std::cout << "=====================================\n\n";

	std::cout << "Basic Types:\n";
	std::cout << "  ipv4_address_t: " << sizeof(ipv4_address_t) << " bytes\n";
	std::cout << "  ipv6_address_t: " << sizeof(ipv6_address_t) << " bytes\n";
	std::cout << "  rte_ether_addr: " << sizeof(rte_ether_addr) << " bytes\n";
	std::cout << "  common::globalBase::tFlow: " << sizeof(common::globalBase::tFlow) << " bytes\n\n";

	std::cout << "Hashtable Value Types:\n";
	std::cout << "  neighbor_value: " << sizeof(neighbor_value) << " bytes\n";
	std::cout << "  fw_state_value_t: " << sizeof(fw_state_value_t) << " bytes\n";
	std::cout << "  nat64stateful_lan_value: " << sizeof(nat64stateful_lan_value) << " bytes\n";
	std::cout << "  nat64stateful_wan_value: " << sizeof(nat64stateful_wan_value) << " bytes\n";
	std::cout << "  balancer_state_value_t: " << sizeof(balancer_state_value_t) << " bytes\n";
	std::cout << "  transport_key_t: " << sizeof(transport_key_t) << " bytes\n";
	std::cout << "  total_key_t: " << sizeof(total_key_t) << " bytes\n";
	std::cout << "  common::Actions: " << sizeof(common::Actions) << " bytes\n\n";

	std::cout << "Value Size Categories:\n";
	std::cout << "  Small (≤16 bytes): ";
	if (sizeof(neighbor_value) <= 16)
		std::cout << "neighbor_value(" << sizeof(neighbor_value) << ") ";
	if (sizeof(nat64stateful_lan_value) <= 16)
		std::cout << "nat64stateful_lan_value(" << sizeof(nat64stateful_lan_value) << ") ";
	if (sizeof(balancer_state_value_t) <= 16)
		std::cout << "balancer_state_value_t(" << sizeof(balancer_state_value_t) << ") ";
	if (sizeof(transport_key_t) <= 16)
		std::cout << "transport_key_t(" << sizeof(transport_key_t) << ") ";
	if (sizeof(total_key_t) <= 16)
		std::cout << "total_key_t(" << sizeof(total_key_t) << ") ";
	if (sizeof(common::Actions) <= 16)
		std::cout << "Actions(" << sizeof(common::Actions) << ") ";
	std::cout << "\n";

	std::cout << "  Medium (17-32 bytes): ";
	if (sizeof(neighbor_value) > 16 && sizeof(neighbor_value) <= 32)
		std::cout << "neighbor_value(" << sizeof(neighbor_value) << ") ";
	if (sizeof(nat64stateful_lan_value) > 16 && sizeof(nat64stateful_lan_value) <= 32)
		std::cout << "nat64stateful_lan_value(" << sizeof(nat64stateful_lan_value) << ") ";
	if (sizeof(nat64stateful_wan_value) > 16 && sizeof(nat64stateful_wan_value) <= 32)
		std::cout << "nat64stateful_wan_value(" << sizeof(nat64stateful_wan_value) << ") ";
	if (sizeof(balancer_state_value_t) > 16 && sizeof(balancer_state_value_t) <= 32)
		std::cout << "balancer_state_value_t(" << sizeof(balancer_state_value_t) << ") ";
	if (sizeof(transport_key_t) > 16 && sizeof(transport_key_t) <= 32)
		std::cout << "transport_key_t(" << sizeof(transport_key_t) << ") ";
	if (sizeof(total_key_t) > 16 && sizeof(total_key_t) <= 32)
		std::cout << "total_key_t(" << sizeof(total_key_t) << ") ";
	if (sizeof(common::Actions) > 16 && sizeof(common::Actions) <= 32)
		std::cout << "Actions(" << sizeof(common::Actions) << ") ";
	std::cout << "\n";

	std::cout << "  Large (>32 bytes): ";
	if (sizeof(fw_state_value_t) > 32)
		std::cout << "fw_state_value_t(" << sizeof(fw_state_value_t) << ") ";
	if (sizeof(nat64stateful_wan_value) > 32)
		std::cout << "nat64stateful_wan_value(" << sizeof(nat64stateful_wan_value) << ") ";
	std::cout << "\n";

	return 0;
}
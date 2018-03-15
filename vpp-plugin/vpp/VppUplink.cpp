/* -*- C++ -*-; c-basic-offset: 4; indent-tabs-mode: nil */
/*
 * Copyright (c) 2017 Cisco Systems, Inc. and others.  All rights reserved.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v1.0 which accompanies this distribution,
 * and is available at http://www.eclipse.org/legal/epl-v10.html
 */

#include "VppUplink.h"

#include "vom/arp_proxy_binding.hpp"
#include "vom/arp_proxy_config.hpp"
#include "vom/interface.hpp"
#include "vom/ip_unnumbered.hpp"
#include "vom/l3_binding.hpp"
#include "vom/lldp_binding.hpp"
#include "vom/lldp_global.hpp"
#include "vom/sub_interface.hpp"
#include "vom/bond_interface.hpp"
#include "vom/bond_member.hpp"

using namespace VOM;

namespace VPP {

static const std::string UPLINK_KEY = "__uplink__";

Uplink::Uplink() : m_type(VLAN) {}

std::shared_ptr<VOM::interface> Uplink::mk_interface(const std::string& uuid,
                                                     uint32_t vnid) {
    std::shared_ptr<VOM::interface> sp;
    switch (m_type) {
    case VXLAN: {
        vxlan_tunnel vt(m_vxlan.src, m_vxlan.dst, vnid);
        VOM::OM::write(uuid, vt);

        return vt.singular();
    }
    case VLAN: {
        sub_interface sb(*m_uplink, interface::admin_state_t::UP, vnid);
        VOM::OM::write(uuid, sb);

        return sb.singular();
    }
    }

    return sp;
}

void Uplink::configure_tap(const route::prefix_t& pfx) {
    tap_interface itf("tapcli-0", interface::admin_state_t::UP, pfx);
    VOM::OM::write(UPLINK_KEY, itf);

    /*
     * commit and L3 Config to the OM so this uplink owns the
     * subnet on the interface. If we don't have a representation
     * of the configured prefix in the OM, we'll sweep it from the
     * interface if we restart
     */
    sub_interface subitf(*m_uplink, interface::admin_state_t::UP, m_vlan);
    l3_binding l3(subitf, pfx);
    OM::commit(UPLINK_KEY, l3);

    ip_unnumbered ipUnnumber(itf, subitf);
    VOM::OM::write(UPLINK_KEY, ipUnnumber);

    arp_proxy_config arpProxyConfig(pfx.low().address().to_v4(),
                                    pfx.high().address().to_v4());
    VOM::OM::write(UPLINK_KEY, arpProxyConfig);

    arp_proxy_binding arpProxyBinding(itf, arpProxyConfig);
    VOM::OM::write(UPLINK_KEY, arpProxyBinding);
}

void Uplink::handle_dhcp_event(dhcp_config_cmds::events_cmd* ec) {
    /*
     * Create the TAP interface with the DHCP learn address.
     *  This allows all traffic punt to VPP to arrive at the TAP/agent.
     */
    std::lock_guard<dhcp_config_cmds::events_cmd> lg(*ec);

    for (auto& msg : *ec) {
        auto& payload = msg.get_payload();

        route::prefix_t pfx(payload.is_ipv6, payload.host_address,
                            payload.mask_width);

        configure_tap(pfx);

        /*
         * VXLAN tunnels use the DHCP address as the source
         */
        m_vxlan.src = pfx.address();
    }

    ec->flush();
}

static VOM::interface::type_t getIntfTypeFromName(std::string& name) {
    if (name.find("Bond") != std::string::npos)
        return VOM::interface::type_t::BOND;
    else if (name.find("Ethernet") != std::string::npos)
        return VOM::interface::type_t::ETHERNET;
    else if (name.find("tap") != std::string::npos)
        return VOM::interface::type_t::TAP;

    return VOM::interface::type_t::AFPACKET;
}

void Uplink::configure(const std::string& fqdn) {
    /*
     * Consruct the uplink physical, so we now 'own' it
     */
    VOM::interface::type_t type = getIntfTypeFromName(m_iface);
    if (VOM::interface::type_t::BOND == type) {
        m_uplink = bond_interface(m_iface, interface::admin_state_t::UP,
                                  bond_interface::mode_t::LACP,
				  bond_interface::lb_t::L2).singular();
        OM::write(UPLINK_KEY, *m_uplink);
        bond_group_binding::enslaved_itf_t slave_itfs;
        for (auto sif : slave_ifaces) {
            std::shared_ptr<interface> itf_ptr = interface(sif,
                getIntfTypeFromName(sif), interface::admin_state_t::UP)
                    .singular();
            OM::write(UPLINK_KEY, *itf_ptr);
            bond_member bm(*itf_ptr, bond_member::mode_t::ACTIVE, bond_member::rate_t::SLOW);
            slave_itfs.insert(bm);
        }
        if (!slave_itfs.empty()) {
           m_bond_group = bond_group_binding(
               *(std::dynamic_pointer_cast<bond_interface>(m_uplink)),
               slave_itfs)
                   .singular();
           OM::write(UPLINK_KEY, *m_bond_group);
        }
    } else {
        m_uplink = interface(m_iface, type, interface::admin_state_t::UP).singular();
        OM::write(UPLINK_KEY, *m_uplink);
    }

    /*
     * Own the v4 and v6 global tables
     */
    route_domain v4_gbl(0);
    OM::write(UPLINK_KEY, v4_gbl);
    route_domain v6_gbl(0);
    OM::write(UPLINK_KEY, v6_gbl);

    /**
     * Enable LLDP on this uplionk
     */
    lldp_global lg(fqdn, 5, 2);
    OM::write(UPLINK_KEY, lg);
    lldp_binding lb(*m_uplink, "uplink-interface");
    OM::write(UPLINK_KEY, lb);

    /*
     * now create the sub-interface on which control and data traffic from
     * the upstream leaf will arrive
     */
    sub_interface subitf(*m_uplink, interface::admin_state_t::UP, m_vlan);
    OM::write(UPLINK_KEY, subitf);

    /**
     * Strip the fully qualified domain name of any domain name
     * to get just the hostname.
     */
    std::string hostname = fqdn;
    std::string::size_type n = hostname.find(".");
    if (n != std::string::npos) {
        hostname = hostname.substr(0, n);
    }

    /**
     * Configure DHCP on the uplink subinterface
     * We must use the MAC address of the uplink interface as the DHCP client-ID
     */
    dhcp_config dc(subitf, hostname, m_uplink->l2_address());
    OM::write(UPLINK_KEY, dc);

    /**
     * In the case of a agent restart, the DHCP process will already be complete
     * in VPP and we won't get notified. So let's cehck here if there alreay
     * exists an L3 config on the interface
     */
    std::deque<std::shared_ptr<l3_binding>> l3s = l3_binding::find(*m_uplink);

    if (l3s.size()) {
        /*
         * there should only be one. we'll pick the first
         */
        std::shared_ptr<l3_binding> l3 = l3s.front();

        /*
         * Claim ownership.
         * VXLAN tunnels use the DHCP address as the source
         */
        OM::commit(UPLINK_KEY, *l3);

        configure_tap(l3->prefix());
        m_vxlan.src = l3->prefix().address();
    }
}

void Uplink::set(const std::string& uplink, uint16_t uplink_vlan,
                 const std::string& encap_name,
                 const boost::asio::ip::address& remote_ip, uint16_t port) {
    m_type = VXLAN;
    m_vxlan.dst = remote_ip;
    m_iface = uplink;
    m_vlan = uplink_vlan;
}

void Uplink::set(const std::string& uplink, uint16_t uplink_vlan,
                 const std::string& encap_name) {
    m_type = VLAN;
    m_iface = uplink;
    m_vlan = uplink_vlan;
}

void Uplink::insert_slave_ifaces(std::string name) {
    this->slave_ifaces.insert(name);
}

void Uplink::add_slave_ifaces(std::unordered_set<std::string> slaves) {
    this->slave_ifaces = slaves;
}

} // namespace VPP

/*
 * Local Variables:
 * eval: (c-set-style "llvm.org")
 * End:
 */

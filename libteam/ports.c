/*
 *   ports.c - Wrapper for team generic netlink port-related communication
 *   Copyright (C) 2012-2015 Jiri Pirko <jiri@resnulli.us>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @ingroup libteam
 * @defgroup ports Team ports functions
 * Wrapper for team generic netlink port-related communication
 *
 * @{
 *
 * Header
 * ------
 * ~~~~{.c}
 * #include <team.h>
 * ~~~~
 */
#include <stdbool.h>
#include <stdlib.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/cli/utils.h>
#include <netlink/cli/link.h>
#include <linux/netdevice.h>
#include <linux/if_team.h>
#include <linux/types.h>
#include <team.h>
#include <private/list.h>
#include <private/misc.h>
#include "team_private.h"

/* \cond HIDDEN_SYMBOLS */

struct team_port {
	struct list_item	list;
	uint32_t		ifindex;
	uint32_t		speed;
	uint8_t			duplex;
	bool			linkup;
	bool			changed;
	bool			removed;
	struct team_ifinfo *	ifinfo;
};

static struct team_port *port_create(struct team_handle *th,
				     uint32_t ifindex)
{
	struct team_port *port;
	int err;

	port = myzalloc(sizeof(struct team_port));
	if (!port) {
		err(th, "Malloc failed.");
		return NULL;
	}
	err = ifinfo_link_with_port(th, ifindex, port, &port->ifinfo);
	if (err) {
		err(th, "Failed to link port with ifinfo.");
		free(port);
		return NULL;
	}
	port->ifindex = ifindex;
	list_add(&th->port_list, &port->list);
	return port;
}

static void port_destroy(struct team_handle *th,
			 struct team_port *port)
{
	if (port->ifinfo)
		ifinfo_unlink(port->ifinfo);
	list_del(&port->list);
	free(port);
}

static void flush_port_list(struct team_handle *th)
{
	struct team_port *port, *tmp;

	list_for_each_node_entry_safe(port, tmp, &th->port_list, list)
		port_destroy(th, port);
}

static void port_list_cleanup_last_state(struct team_handle *th)
{
	struct team_port *port;
	struct team_port *tmp;

	list_for_each_node_entry_safe(port, tmp, &th->port_list, list) {
		port->changed = false;
		if (port->removed)
			port_destroy(th, port);
	}
}

static struct team_port *find_port(struct team_handle *th, uint32_t ifindex)
{
	struct team_port *port;

	list_for_each_node_entry(port, &th->port_list, list)
		if (port->ifindex == ifindex)
			return port;
	return NULL;
}

int get_port_list_handler(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct team_handle *th = arg;
	struct nlattr *attrs[TEAM_ATTR_MAX + 1];
	struct nlattr *nl_port;
	struct nlattr *port_attrs[TEAM_ATTR_PORT_MAX + 1];
	int i;
	uint32_t team_ifindex = 0;

	genlmsg_parse(nlh, 0, attrs, TEAM_ATTR_MAX, NULL);
	if (attrs[TEAM_ATTR_TEAM_IFINDEX])
		team_ifindex = nla_get_u32(attrs[TEAM_ATTR_TEAM_IFINDEX]);

	if (team_ifindex != th->ifindex)
		return NL_SKIP;

	if (!attrs[TEAM_ATTR_LIST_PORT])
		return NL_SKIP;

	if (!th->msg_recv_started) {
		port_list_cleanup_last_state(th);
		th->msg_recv_started = true;
	}
	nla_for_each_nested(nl_port, attrs[TEAM_ATTR_LIST_PORT], i) {
		struct team_port *port;
		uint32_t ifindex;

		if (nla_parse_nested(port_attrs, TEAM_ATTR_PORT_MAX,
				     nl_port, NULL)) {
			err(th, "Failed to parse nested attributes.");
			return NL_SKIP;
		}

		if (!port_attrs[TEAM_ATTR_PORT_IFINDEX]) {
			err(th, "ifindex port attribute not found.");
			return NL_SKIP;
		}

		ifindex = nla_get_u32(port_attrs[TEAM_ATTR_PORT_IFINDEX]);
		port = find_port(th, ifindex);
		if (!port) {
			port = port_create(th, ifindex);
			if (!port)
				return NL_SKIP;
		}
		port->changed = port_attrs[TEAM_ATTR_PORT_CHANGED] ? true : false;
		port->linkup = port_attrs[TEAM_ATTR_PORT_LINKUP] ? true : false;
		port->removed = port_attrs[TEAM_ATTR_PORT_REMOVED] ? true : false;
		if (port_attrs[TEAM_ATTR_PORT_SPEED])
			port->speed = nla_get_u32(port_attrs[TEAM_ATTR_PORT_SPEED]);
		if (port_attrs[TEAM_ATTR_PORT_DUPLEX])
			port->duplex = nla_get_u8(port_attrs[TEAM_ATTR_PORT_DUPLEX]);
	}

	set_call_change_handlers(th, TEAM_PORT_CHANGE);
	return NL_SKIP;
}

static int get_port_list(struct team_handle *th)
{
	struct nl_msg *msg;
	int err;

	msg = nlmsg_alloc();
	if (!msg)
		return -ENOMEM;

	genlmsg_put(msg, NL_AUTO_PID, th->nl_sock_seq, th->family, 0, 0,
			 TEAM_CMD_PORT_LIST_GET, 0);
	NLA_PUT_U32(msg, TEAM_ATTR_TEAM_IFINDEX, th->ifindex);

	th->msg_recv_started = false;
	err = send_and_recv(th, msg, get_port_list_handler, th);
	if (err)
		return err;

	return check_call_change_handlers(th, TEAM_PORT_CHANGE);

nla_put_failure:
	nlmsg_free(msg);
	return -ENOBUFS;
}

int port_list_alloc(struct team_handle *th)
{
	list_init(&th->port_list);

	return 0;
}

int port_list_init(struct team_handle *th)
{
	int err;

	err = get_port_list(th);
	if (err) {
		err(th, "Failed to get port list.");
		return err;
	}
	return 0;
}

void port_list_free(struct team_handle *th)
{
	flush_port_list(th);
}

void port_unlink(struct team_port *port)
{
	port->ifinfo = NULL;
}

/* \endcond */

/**
 * @param th		libteam library context
 * @param port		port structure
 *
 * @details Get next port in list.
 *
 * @return Port next to port passed.
 **/
TEAM_EXPORT
struct team_port *team_get_next_port(struct team_handle *th,
				     struct team_port *port)
{
	return list_get_next_node_entry(&th->port_list, port, list);
}

/**
 * @param port		port structure
 *
 * @details Get port interface index.
 *
 * @return Port interface index as idenfified by in kernel.
 **/
TEAM_EXPORT
uint32_t team_get_port_ifindex(struct team_port *port)
{
	return port->ifindex;
}

/**
 * @param port		port structure
 *
 * @details Get port speed.
 *
 * @return Port speed in Mbits/s.
 **/
TEAM_EXPORT
uint32_t team_get_port_speed(struct team_port *port)
{
	return port->speed;
}

/**
 * @param port		port structure
 *
 * @details Get port duplex.
 *
 * @return 0 = half-duplex, 1 = full-duplex
 **/
TEAM_EXPORT
uint8_t team_get_port_duplex(struct team_port *port)
{
	return port->duplex;
}

/**
 * @param port		port structure
 *
 * @details See if port link is up.
 *
 * @return True if port link is up.
 **/
TEAM_EXPORT
bool team_is_port_link_up(struct team_port *port)
{
	return port->linkup;
}

/**
 * @param port		port structure
 *
 * @details See if port values got changed.
 *
 * @return True if port got changed.
 **/
TEAM_EXPORT
bool team_is_port_changed(struct team_port *port)
{
	return port->changed;
}

/**
 * @param port		port structure
 *
 * @details See if port was removed.
 *
 * @return True if port was removed.
 **/
TEAM_EXPORT
bool team_is_port_removed(struct team_port *port)
{
	return port->removed;
}

/**
 * @param port		port structure
 *
 * @details Get port rtnetlink interface info.
 *
 * @return Pointer to appropriate team_ifinfo structure.
 **/
TEAM_EXPORT
struct team_ifinfo *team_get_port_ifinfo(struct team_port *port)
{
	return port->ifinfo;
}

/**
 * @param th		libteam library context
 * @param port		port structure
 *
 * @details See if port is actually present in this team.
 *
 * @return True if port is present at a moment.
 **/
TEAM_EXPORT
bool team_is_port_present(struct team_handle *th, struct team_port *port)
{
	struct team_ifinfo *ifinfo = team_get_port_ifinfo(port);

	return team_get_ifinfo_master_ifindex(ifinfo) == th->ifindex &&
	       !team_is_port_removed(port);
}

/**
 * @}
 */

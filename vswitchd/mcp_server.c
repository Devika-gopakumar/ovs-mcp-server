#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "vswitch-idl.h"
#include "ovsdb-idl.h"
#include "openvswitch/json.h"
#include "openvswitch/shash.h"
#include "openvswitch/vlog.h"
#include "openvswitch/util.h"
#include "mcp_server.h"
#include "bridge.h"

VLOG_DEFINE_THIS_MODULE(mcp_server);

#define MCP_PORT 8080
#define MAX_CLIENTS 5
#define BUFFER_SIZE 4096

static int listen_fd = -1;

/* Build and send an HTTP/JSON response to the client */
static void
reply_json(int fd, int http_code, const char *http_status,
           struct json *body)
{
    char *body_str = json_to_string(body, 0);
    char headers[BUFFER_SIZE];

    snprintf(headers, sizeof(headers),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: application/json\r\n"
             "\r\n"
             "%s",
             http_code, http_status, body_str);

    if (write(fd, headers, strlen(headers)) < 0) {
        VLOG_WARN("reply_json: write failed: %s", ovs_strerror(errno));
    }
    free(body_str);
}

/* Send a JSON error response */
static void
reply_error(int fd, int http_code, const char *http_status,
            const char *msg)
{
    struct json *obj = json_object_create();
    json_object_put_string(obj, "error", msg);
    reply_json(fd, http_code, http_status, obj);
    json_destroy(obj);
}

/* Tool: list all ports across all bridges */
static void
tool_get_ports(int fd, struct ovsdb_idl *idl)
{
    if (!idl || !ovsdb_idl_has_ever_connected(idl)) {
        reply_error(fd, 503, "Service Unavailable", "database not ready");
        return;
    }

    struct json *response = json_object_create();
    json_object_put_string(response, "action", "switch.get_ports");

    struct json *port_list = json_array_create_empty();

    const struct ovsrec_bridge *bridge;
    OVSREC_BRIDGE_FOR_EACH(bridge, idl) {
        if (!bridge || !bridge->ports) continue;

        for (size_t i = 0; i < bridge->n_ports; i++) {
            struct ovsrec_port *p = bridge->ports[i];
            if (!p || !p->interfaces) continue;

            for (size_t j = 0; j < p->n_interfaces; j++) {
                struct ovsrec_interface *ifc = p->interfaces[j];
                if (!ifc) continue;

                struct json *item = json_object_create();
                json_object_put_string(item, "name",
                    ifc->name ? ifc->name : "unknown");
                json_object_put_string(item, "type",
                    ifc->type ? ifc->type : "system");
                json_object_put_string(item, "bridge",
                    bridge->name ? bridge->name : "unknown");

                json_array_add(port_list, item);
            }
        }
    }

    json_object_put(response, "data", port_list);
    reply_json(fd, 200, "OK", response);
    json_destroy(response);
}

/* Tool: dump flow tables from all bridges */
static void
tool_get_flows(int fd)
{
    char *flow_dump = bridge_get_all_flows();

    struct json *response = json_object_create();
    json_object_put_string(response, "tool", "get_flows");
    json_object_put_string(response, "flows",
                           flow_dump ? flow_dump : "");

    reply_json(fd, 200, "OK", response);
    json_destroy(response);
    free(flow_dump);
}

/* Tool: get per-port packet/byte statistics */
static void
tool_get_port_stats(int fd)
{
    struct json *response = json_object_create();
    json_object_put_string(response, "tool", "get_port_stats");
    json_object_put(response, "stats", bridge_get_port_stats());
    reply_json(fd, 200, "OK", response);
    json_destroy(response);
}

/* Tool: set VLAN tag on a port */
static void
tool_set_vlan(int fd, struct json *args)
{
    struct json *port_val = shash_find_data(args->object, "port");
    if (!port_val || port_val->type != JSON_STRING) {
        reply_error(fd, 400, "Bad Request", "missing port argument");
        return;
    }

    struct json *vlan_val = shash_find_data(args->object, "vlan");
    if (!vlan_val || vlan_val->type != JSON_INTEGER) {
        reply_error(fd, 400, "Bad Request", "missing vlan argument");
        return;
    }

    const char *pname = json_string(port_val);
    int64_t vid = vlan_val->integer;

    if (bridge_set_vlan(pname, vid) != 0) {
        reply_error(fd, 404, "Not Found", "port not found");
        return;
    }

    struct json *response = json_object_create();
    json_object_put_string(response, "tool", "set_vlan");
    json_object_put_string(response, "port", pname);
    json_object_put(response, "vlan", json_integer_create(vid));
    json_object_put_string(response, "status", "ok");
    reply_json(fd, 200, "OK", response);
    json_destroy(response);
}

/* Tool: bring a port up or down */
static void
tool_set_port_state(int fd, struct json *args)
{
    struct json *port_val = shash_find_data(args->object, "port");
    if (!port_val || port_val->type != JSON_STRING) {
        reply_error(fd, 400, "Bad Request", "missing port argument");
        return;
    }

    struct json *state_val = shash_find_data(args->object, "state");
    if (!state_val || state_val->type != JSON_STRING) {
        reply_error(fd, 400, "Bad Request", "missing state argument");
        return;
    }

    const char *pname  = json_string(port_val);
    const char *state  = json_string(state_val);
    bool enable;

    if (strcmp(state, "up") == 0) {
        enable = true;
    } else if (strcmp(state, "down") == 0) {
        enable = false;
    } else {
        reply_error(fd, 400, "Bad Request", "state must be 'up' or 'down'");
        return;
    }

    if (bridge_set_port_state(pname, enable) != 0) {
        reply_error(fd, 404, "Not Found", "port not found");
        return;
    }

    struct json *response = json_object_create();
    json_object_put_string(response, "tool",   "set_port_state");
    json_object_put_string(response, "port",   pname);
    json_object_put_string(response, "state",  state);
    json_object_put_string(response, "status", "ok");
    reply_json(fd, 200, "OK", response);
    json_destroy(response);
}

/* Route incoming request to the correct tool handler */
static void
dispatch_request(int fd, const char *body, struct ovsdb_idl *idl)
{
    struct json *req = json_from_string(body);
    if (!req || req->type != JSON_OBJECT) {
        reply_error(fd, 400, "Bad Request", "invalid JSON");
        json_destroy(req);
        return;
    }

    struct json *tool_val = shash_find_data(req->object, "tool");
    if (!tool_val || tool_val->type != JSON_STRING) {
        reply_error(fd, 400, "Bad Request", "missing tool field");
        json_destroy(req);
        return;
    }

    const char *tool = json_string(tool_val);
    VLOG_INFO("MCP request: tool=%s", tool);

    struct json *args = shash_find_data(req->object, "arguments");

    if (strcmp(tool, "get_ports") == 0) {
        tool_get_ports(fd, idl);
    } else if (strcmp(tool, "get_flows") == 0) {
        tool_get_flows(fd);
    } else if (strcmp(tool, "get_port_stats") == 0) {
        tool_get_port_stats(fd);
    } else if (strcmp(tool, "set_vlan") == 0) {
        if (!args || args->type != JSON_OBJECT) {
            reply_error(fd, 400, "Bad Request", "missing arguments");
        } else {
            tool_set_vlan(fd, args);
        }
    } else if (strcmp(tool, "set_port_state") == 0) {
        if (!args || args->type != JSON_OBJECT) {
            reply_error(fd, 400, "Bad Request", "missing arguments");
        } else {
            tool_set_port_state(fd, args);
        }
    } else {
        reply_error(fd, 404, "Not Found", "unknown tool");
    }

    json_destroy(req);
}

/* Initialize the MCP HTTP server */
void
mcp_server_init(void)
{
    struct sockaddr_in addr;
    int opt = 1;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        VLOG_ERR("mcp_server_init: socket failed: %s", ovs_strerror(errno));
        return;
    }

    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MCP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        VLOG_ERR("mcp_server_init: bind failed: %s", ovs_strerror(errno));
        close(listen_fd);
        listen_fd = -1;
        return;
    }

    listen(listen_fd, MAX_CLIENTS);
    fcntl(listen_fd, F_SETFL, O_NONBLOCK);

    VLOG_INFO("MCP Server started successfully on port %d", MCP_PORT);
}

/* Called every OVS main loop iteration — handles one request if pending */
void
mcp_server_run(struct ovsdb_idl *idl)
{
    if (listen_fd < 0) return;

    char buf[BUFFER_SIZE];
    char method[16], path[256];

    int conn_fd = accept(listen_fd, NULL, NULL);
    if (conn_fd < 0) return;

    int bytes = read(conn_fd, buf, sizeof(buf) - 1);
    if (bytes <= 0) {
        close(conn_fd);
        return;
    }
    buf[bytes] = '\0';

    if (sscanf(buf, "%15s %255s", method, path) != 2) {
        close(conn_fd);
        return;
    }

    if (strcmp(method, "POST") != 0 || strcmp(path, "/mcp") != 0) {
        reply_error(conn_fd, 404, "Not Found", "not found");
        close(conn_fd);
        return;
    }

    char *body = strstr(buf, "\r\n\r\n");
    if (!body) {
        reply_error(conn_fd, 400, "Bad Request", "no body");
        close(conn_fd);
        return;
    }
    body += 4;

    dispatch_request(conn_fd, body, idl);
    close(conn_fd);
}

/* Shut down the MCP server */
void
mcp_server_close(void)
{
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
        VLOG_INFO("MCP Server stopped");
    }
}

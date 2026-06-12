# Distributed MCP Fabric for Open vSwitch

A lightweight **MCP (Model Context Protocol) server** embedded inside **Open vSwitch (OVS)** in C, enabling AI agents to query and control a virtual network switch using natural language.

---

## Architecture

```
        LLM / AI Agent (Gemini 2.5 Flash)
                    |
            MCP (multi-endpoint)
                    |
       +------------+------------+
       |            |            |
  MCP Server   MCP Server   MCP Server
  (Node 1)     (Node 2)     (Node 3)
       |            |            |
  Open vSwitch Open vSwitch Open vSwitch
```

Each node = `[ MCP Server ] + [ Open vSwitch ]`

---

## Modules

### Module 1 — OVS Build and Understanding
- Built Open vSwitch from source on Kali Linux
- Build pipeline: `./boot.sh` → `./configure` → `make -j4` → `sudo make install`
- Created virtual bridge `br0` and test port `test-port`
- Learned: bridges, ports, interfaces, flows, VLANs

### Module 2 — MCP Server Integration
- Created `mcp_server.c` — lightweight HTTP server embedded inside OVS
- Listens on port **8080**, endpoint: `POST /mcp`
- Hooked into OVS main loop inside `ovs-vswitchd.c`:
  - `mcp_server_init()` — called once at OVS startup
  - `mcp_server_run(idl)` — called every main loop iteration
  - `mcp_server_close()` — called at OVS shutdown
- Used non-blocking socket so OVS main loop never freezes

### Module 3 — MCP Dispatcher
- JSON parsing using OVS built-in `openvswitch/json.h`
- `dispatch_request()` reads the `tool` field and routes to correct handler
- Full error handling for invalid JSON, missing fields, unknown tools

### Module 4 — Connect MCP to OVS Internals
- `get_ports` — reads bridge, port, interface hierarchy from OVSDB IDL
- `get_flows` — calls `bridge_get_all_flows()` using ofproto API
- `get_port_stats` — calls `bridge_get_port_stats()` reading kernel netdev counters

### Module 5 — SET Operations
- `set_vlan` — writes VLAN tag to OVSDB then applies to datapath via `port_configure()`
- `set_port_state` — enables or disables port using `NETDEV_UP` flag

### Module 6 — LLM Integration
- Python AI agent `ovs_agent.py` powered by **Gemini 2.5 Flash**
- Plain English → Gemini decides tool → OVS executes → Gemini explains result
- Three functions: `ask_gemini()`, `call_ovs_tool()`, `explain_result()`

---

## Files

| File | Description |
|---|---|
| `vswitchd/mcp_server.c` | NEW — HTTP server and all 5 tool handlers |
| `vswitchd/mcp_server.h` | NEW — function declarations |
| `vswitchd/bridge.c` | MODIFIED — added get_flows, get_port_stats, set_vlan, set_port_state |
| `vswitchd/bridge.h` | MODIFIED — declared new bridge functions |
| `vswitchd/ovs-vswitchd.c` | MODIFIED — added MCP init, run, close calls |
| `vswitchd/automake.mk` | MODIFIED — added mcp_server.c to build |
| `ovs_agent.py` | NEW — Python LLM agent |

---

## MCP Tools

| Tool | Description | Arguments |
|---|---|---|
| `get_ports` | List all ports on the switch | None |
| `get_flows` | Dump all flow rules | None |
| `get_port_stats` | Packet and byte counters per port | None |
| `set_vlan` | Set VLAN tag on a port | port, vlan |
| `set_port_state` | Enable or disable a port | port, state (up/down) |

---

## Build and Run

### Build OVS
```bash
cd ~/ovs
./boot.sh
./configure
make -j4
sudo make install
```

### Start OVS
```bash
sudo ovsdb-server \
  --remote=punix:/usr/local/var/run/openvswitch/db.sock \
  --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
  --pidfile --detach

sudo ovs-vswitchd --pidfile --detach
```

### Create test bridge and port
```bash
sudo ovs-vsctl add-br br0
sudo ovs-vsctl add-port br0 test-port -- set interface test-port type=internal
```

---

## Test with curl

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"tool": "get_ports"}'

curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"tool": "get_flows"}'

curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"tool": "get_port_stats"}'

curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"tool": "set_vlan", "arguments": {"port": "test-port", "vlan": 100}}'

curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"tool": "set_port_state", "arguments": {"port": "test-port", "state": "down"}}'

curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"tool": "set_port_state", "arguments": {"port": "test-port", "state": "up"}}'
```

---

## Run AI Agent

```bash
pip install google-genai --break-system-packages
python3 ovs_agent.py
```

### Natural language commands
```
show me all ports
show me all flows
show me port statistics
disable the test-port
enable the test-port
set vlan 200 on test-port
```

---

## Tech Stack

| Component | Technology |
|---|---|
| OS | Kali Linux |
| Switch | Open vSwitch 3.7.90 |
| MCP Server | C (embedded inside OVS) |
| AI Agent | Python |
| LLM | Gemini 2.5 Flash (google-genai) |
| Protocol | HTTP/JSON on port 8080 |
| JSON Library | OVS built-in openvswitch/json.h |

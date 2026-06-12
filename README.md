# Distributed MCP Fabric for Open vSwitch

A lightweight MCP server embedded inside Open vSwitch (OVS) in C, enabling AI agents to control a virtual network switch using natural language.

## Modules
- Module 1: OVS Build and Understanding
- Module 2: MCP Server Integration (HTTP server on port 8080)
- Module 3: MCP Dispatcher (JSON parsing and tool routing)
- Module 4: Connect MCP to OVS Internals (real data)
- Module 5: SET Operations (set VLAN, port state)
- Module 6: LLM Integration (Gemini AI controls switch)

## Tools
- get_ports, get_flows, get_port_stats, set_vlan, set_port_state

## Run
Start OVS, then: python3 ovs_agent.py

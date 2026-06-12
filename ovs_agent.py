from google import genai
import requests
import json
import time

# ── Config ──────────────────────────────────────────────
API_KEY = "AIzaSyD795JkcNqDtmGrGWAWWH0yIXmnpGISulg"
MCP_URL = "http://localhost:8080/mcp"
MODEL   = "gemini-2.0-flash"
# ────────────────────────────────────────────────────────

client = genai.Client(api_key=API_KEY)

def call_ovs_tool(tool_name, arguments=None):
    payload = {"tool": tool_name}
    if arguments:
        payload["arguments"] = arguments
    try:
        response = requests.post(MCP_URL, json=payload, timeout=5)
        return response.json()
    except Exception as e:
        return {"error": str(e)}

def ask_gemini(user_input):
    prompt = f"""
You are an AI assistant controlling an Open vSwitch (OVS) network switch.
You have these tools available:
- get_ports: Get all ports on the switch
- get_flows: Get all flow rules on the switch
- get_port_stats: Get packet and byte statistics for all ports
- set_vlan: Set a VLAN tag on a port. Needs port name and vlan number.
- set_port_state: Enable or disable a port. Needs port name and state up or down.

The user says: "{user_input}"

Reply ONLY with a JSON object. No explanation. No markdown. No code blocks.
Examples:
{{"tool": "get_ports"}}
{{"tool": "get_flows"}}
{{"tool": "get_port_stats"}}
{{"tool": "set_vlan", "arguments": {{"port": "test-port", "vlan": 100}}}}
{{"tool": "set_port_state", "arguments": {{"port": "test-port", "state": "down"}}}}
{{"tool": "none", "message": "I don't understand that request"}}
"""
    for attempt in range(3):
        try:
            response = client.models.generate_content(
                model=MODEL,
                contents=prompt
            )
            return response.text.strip()
        except Exception as e:
            if attempt < 2:
                print("Gemini busy, retrying...")
                time.sleep(3)
            else:
                return '{"tool": "none", "message": "Gemini unavailable, try again"}'

def explain_result(user_input, tool_name, result):
    prompt = f"""
The user asked: "{user_input}"
We called the OVS tool: {tool_name}
The OVS switch returned: {json.dumps(result)}

Explain this result to the user in simple friendly English.
Keep it to 2 to 4 sentences maximum.
"""
    for attempt in range(3):
        try:
            response = client.models.generate_content(
                model=MODEL,
                contents=prompt
            )
            return response.text.strip()
        except Exception as e:
            if attempt < 2:
                print("Gemini busy, retrying...")
                time.sleep(3)
            else:
                return f"OVS returned: {json.dumps(result)}"

def main():
    print("=" * 50)
    print("  OVS AI Agent — powered by Gemini 2.5 Flash")
    print("  Type 'quit' to exit")
    print("=" * 50)
    print()

    while True:
        user_input = input("You: ").strip()

        if user_input.lower() in ["quit", "exit", "q"]:
            print("Goodbye!")
            break

        if not user_input:
            continue

        print("Thinking...")

        gemini_reply = ask_gemini(user_input)
        gemini_reply = gemini_reply.replace("```json", "").replace("```", "").strip()

        try:
            decision = json.loads(gemini_reply)
        except json.JSONDecodeError:
            print(f"Agent: Sorry I could not understand that.\n")
            continue

        tool_name = decision.get("tool")

        if tool_name == "none":
            print(f"Agent: {decision.get('message', 'Unknown request')}\n")
            continue

        arguments = decision.get("arguments")
        print(f"Calling OVS tool: {tool_name}...")
        result = call_ovs_tool(tool_name, arguments)

        explanation = explain_result(user_input, tool_name, result)
        print(f"Agent: {explanation}\n")

if __name__ == "__main__":
    main()

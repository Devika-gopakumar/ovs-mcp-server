#ifndef MCP_SERVER_H
#define MCP_SERVER_H

struct ovsdb_idl;

void mcp_server_init(void);
void mcp_server_run(struct ovsdb_idl *idl);
void mcp_server_close(void);

#endif

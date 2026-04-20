"""
MCP Bridge Server - Den + Quat only
"""


import asyncio
import json
import threading
import time
import uuid
from flask import Flask, jsonify, request
import websockets
import ssl


# =====================================================================
# CAU HINH
# =====================================================================
MCP_ENDPOINT = "wss://api.xiaozhi.me/mcp/?token=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOjY5OTE2NCwiYWdlbnRJZCI6MTE2NTE2OCwiZW5kcG9pbnRJZCI6ImFnZW50XzExNjUxNjgiLCJwdXJwb3NlIjoibWNwLWVuZHBvaW50IiwiaWF0IjoxNzczNTY3ODA1LCJleHAiOjE4MDUxMjU0MDV9.JkgD8MxmG8ZGZbQ0P47cxl6fcgoRtNaUCgwm_fE238Vhgl-uyJWb5a1i448IbcdZ2S1FDZEDlRHjVGcQgDLRPg"
HTTP_PORT = 8080


# =====================================================================
# TRANG THAI
# =====================================================================
pending_commands = []
ws_connection    = None
lock             = threading.Lock()
pending_results  = {}
main_loop        = None

# Trang thai thiet bi (duoc cap nhat moi khi Xiaozhi ra lenh hoac app gui lenh)
device_states = {
    "fan":              "off",
    "living_room_light": "off",
    "bedroom_light":    "off",
    "kitchen_light":    "off",
    "clothesline":      "retracted",  # "extended" hoac "retracted"
}


# =====================================================================
# TOOLS - Chi den va quat
# =====================================================================
TOOLS = [
    {
        "name": "fan_control",
        "description": "Dieu khien quat",
        "inputSchema": {
            "type": "object",
            "properties": {"state": {"type": "string", "enum": ["on", "off"]}},
            "required": ["state"]
        }
    },
    {
        "name": "living_room_lights_control",
        "description": "Dieu khien den phong khach",
        "inputSchema": {
            "type": "object",
            "properties": {"state": {"type": "string", "enum": ["on", "off"]}},
            "required": ["state"]
        }
    },
    {
        "name": "bedroom_lights_control",
        "description": "Dieu khien den phong ngu",
        "inputSchema": {
            "type": "object",
            "properties": {"state": {"type": "string", "enum": ["on", "off"]}},
            "required": ["state"]
        }
    },
    {
        "name": "kitchen_lights_control",
        "description": "Dieu khien den phong bep",
        "inputSchema": {
            "type": "object",
            "properties": {"state": {"type": "string", "enum": ["on", "off"]}},
            "required": ["state"]
        }
    },
    {
        "name": "clothesline_control",
        "description": "Day / keo day phoi do (servo)",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string", "enum": ["retract", "extend"]}
            },
            "required": ["action"]
        }
    },
    {
        "name": "get_environment",
        "description": "Doc nhiet do va do am tu cam bien DHT11",
        "inputSchema": {
            "type": "object",
            "properties": {},
            "required": []
        }
    },
    {
        "name": "get_devices_status",
        "description": (
            "Đọc trạng thái hiện tại của tất cả thiết bị điện trong nhà: "
            "quạt (đang chạy hay tắt), đèn phòng khách, đèn phòng ngủ, đèn nhà bếp, đèn sân. "
            "Dùng khi người dùng hỏi: đèn có sáng không, quạt có đang chạy không, "
            "phòng khách bật đèn chưa, đèn phòng ngủ tắt chưa, thiết bị nào đang bật, "
            "nhà đang bật gì, kiểm tra đèn quạt."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {},
            "required": []
        }
    },
    {
        "name": "get_sensors_status",
        "description": (
            "Đọc trạng thái tất cả cảm biến trong nhà: "
            "cảm biến mưa (trời có mưa không, mức độ mưa bao nhiêu phần trăm), "
            "cảm biến lửa / cháy (có phát hiện lửa hoặc cháy không, còi báo động có đang kêu không), "
            "và trạng thái giàn phơi / dây phơi (đang mở ra phơi đồ hay đã thu vào do mưa). "
            "Dùng khi người dùng hỏi: trời có mưa không, cảm biến mưa thế nào, "
            "có cháy không, có lửa không, còi báo cháy có kêu không, "
            "giàn phơi đang ở đâu, dây phơi đã thu vào chưa, đồ phơi bị ướt chưa."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {},
            "required": []
        }
    },
]


# =====================================================================
# DISPATCH
# =====================================================================
def dispatch_command(tool, args, timeout=15):
    cmd_id  = str(uuid.uuid4())[:8]
    cmd     = {"id": cmd_id, "tool": tool, "args": args, "action": "execute"}
    t_event = threading.Event()


    with lock:
        pending_results[cmd_id] = {"thread_event": t_event, "result": {}}
        pending_commands.append(cmd)


    print(f"[CMD] Dispatch: {tool} | id={cmd_id} | t={time.strftime('%H:%M:%S')}")
    fired = t_event.wait(timeout=timeout)


    with lock:
        entry  = pending_results.pop(cmd_id, {})
        result = entry.get("result", {})
        if not fired:
            pending_commands[:] = [c for c in pending_commands if c["id"] != cmd_id]


    if not fired:
        print(f"[CMD] TIMEOUT: {tool} id={cmd_id}")
        return {"success": False, "error": "R4 timeout"}

    print(f"[CMD] Result: {tool} -> {result}")

    # Cap nhat device_states neu la lenh dieu khien thiet bi
    if result.get("success") or "error" not in result:
        _sync_device_state(tool, args)

    return result


def _sync_device_state(tool, args):
    """Dong bo trang thai thiet bi vao device_states sau khi lenh thanh cong."""
    with lock:
        if tool == "fan_control":
            device_states["fan"] = args.get("state", device_states["fan"])
        elif tool == "living_room_lights_control":
            device_states["living_room_light"] = args.get("state", device_states["living_room_light"])
        elif tool == "bedroom_lights_control":
            device_states["bedroom_light"] = args.get("state", device_states["bedroom_light"])
        elif tool == "kitchen_lights_control":
            device_states["kitchen_light"] = args.get("state", device_states["kitchen_light"])
        elif tool == "clothesline_control":
            action = args.get("action", "")
            if action == "extend":
                device_states["clothesline"] = "extended"
            elif action == "retract":
                device_states["clothesline"] = "retracted"


# =====================================================================
# FLASK
# =====================================================================
app = Flask(__name__)


@app.route('/poll', methods=['GET'])
def poll():
    deadline = time.time() + 8.0
    while time.time() < deadline:
        with lock:
            if pending_commands:
                cmd = pending_commands.pop(0)
                print(f"[HTTP] R4 lay lenh: {cmd['tool']}")
                return jsonify(cmd)
        time.sleep(0.05)
    return jsonify({"action": "none"})


@app.route('/result', methods=['POST'])
def result():
    data = request.get_json()
    if not data:
        return jsonify({"ok": False})


    # R4 gui "id" luon la chuoi trong JSON; Flask co the parse thanh int — key pending_results la str
    raw_id   = data.get("id")
    cmd_id   = str(raw_id) if raw_id is not None else None
    res_data = data.get("result", {})
    print(f"[HTTP] Ket qua: id={cmd_id}, data={res_data}")


    with lock:
        if cmd_id and cmd_id in pending_results:
            pending_results[cmd_id]["result"] = res_data


            if main_loop and not main_loop.is_closed():
                ev = pending_results[cmd_id].get("asyncio_event")
                if ev:
                    main_loop.call_soon_threadsafe(ev.set)


            t_ev = pending_results[cmd_id].get("thread_event")
            if t_ev:
                t_ev.set()
        else:
            print(f"[HTTP] Canh bao: id={cmd_id} khong tim thay (co the da timeout)")


    return jsonify({"ok": True})


@app.route('/status', methods=['GET'])
def status():
    return jsonify({
        "ws_connected":     ws_connection is not None,
        "pending_commands": len(pending_commands),
    })


@app.route('/command', methods=['POST'])
def command():
    data = request.get_json()
    if not data or 'tool' not in data:
        return jsonify({"success": False, "error": "Missing tool"}), 400
    tool   = data["tool"]
    args   = data.get("args", {})
    result = dispatch_command(tool, args)
    # Dong bo trang thai ngay ca khi lenh den tu app (khong qua Xiaozhi)
    if result.get("success") or "error" not in result:
        _sync_device_state(tool, args)
    return jsonify(result)


@app.route('/sensors', methods=['GET'])
def sensors():
    """Doc du lieu DHT11 tu R4 qua lenh get_environment."""
    result = dispatch_command("get_environment", {})
    if result.get("success") is False and "error" in result:
        return jsonify({"success": False, "error": result["error"]})
    # R4 tra ve: {"temperature": 28.5, "humidity": 65, "success": true}
    return jsonify(result)


def background_sync_r4():
    """Chay ngam moi 20s de lay trang thai thiet bi tu R4, dac biet cho truong hop gian phoi tu dong thu vao khi mua"""
    while True:
        time.sleep(20)
        result = dispatch_command("get_devices_status", {}, timeout=10)
        if result.get("success") and "error" not in result:
            with lock:
                if "fan" in result: device_states["fan"] = result["fan"]
                if "living_room_light" in result: device_states["living_room_light"] = result["living_room_light"]
                if "bedroom_light" in result: device_states["bedroom_light"] = result["bedroom_light"]
                if "kitchen_light" in result: device_states["kitchen_light"] = result["kitchen_light"]
                # Khong cap nhat clothesline vi R4 get_devices_status chua co, se dung cache

@app.route('/devices_status', methods=['GET'])
def devices_status():
    """Tra ve trang thai thiet bi. Luon luon doc tu cache de App lay duoc realtime (do App poll moi 3s)
    ma khong lam treo R4. Trang thai nay ngay lap tuc update khi Xiaozhi ra lenh hoac sau 20s background sync."""
    with lock:
        snapshot = dict(device_states)
    snapshot["success"] = True
    snapshot["source"] = "cache_realtime"
    return jsonify(snapshot)


@app.route('/schedules', methods=['GET'])
def get_schedules():
    """Placeholder - tra ve danh sach schedule (neu co)."""
    return jsonify({"success": True, "schedules": []})


def run_flask():
    print(f"[HTTP] Flask chay tai port {HTTP_PORT}")
    app.run(host='0.0.0.0', port=HTTP_PORT, debug=False, use_reloader=False, threaded=True)


# =====================================================================
# WEBSOCKET
# =====================================================================
async def handle_mcp(websocket):
    global ws_connection
    ws_connection = websocket
    print("[WS] Ket noi Xiaozhi thanh cong!")
    try:
        async for message in websocket:
            await process_message(websocket, message)
    except websockets.exceptions.ConnectionClosed as e:
        print(f"[WS] Mat ket noi: {e}")
    finally:
        ws_connection = None
        print("[WS] Dong ket noi")


async def process_message(ws, message):
    try:
        msg = json.loads(message)
    except Exception:
        print(f"[WS] JSON loi: {message[:100]}")
        return


    method = msg.get("method", "")
    msg_id = msg.get("id")
    print(f"[WS] Nhan: method={method}, id={msg_id}")


    if method == "initialize":
        await ws.send(json.dumps({
            "jsonrpc": "2.0", "id": msg_id,
            "result": {
                "protocolVersion": "2024-11-05",
                "capabilities": {
                    "experimental": {},
                    "prompts":   {"listChanged": False},
                    "resources": {"subscribe": False, "listChanged": False},
                    "tools":     {"listChanged": False}
                },
                "serverInfo": {"name": "Arduino-R4-Bridge", "version": "4.0"}
            }
        }))
        await ws.send(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}))


    elif method == "ping":
        await ws.send(json.dumps({"jsonrpc": "2.0", "id": msg_id, "result": {}}))


    elif method == "tools/list":
        await ws.send(json.dumps({
            "jsonrpc": "2.0", "id": msg_id,
            "result": {"tools": TOOLS}
        }))
        print(f"[WS] Gui {len(TOOLS)} tools")


    elif method == "resources/list":
        # MCP client (Xiaozhi…) thường gọi — phải trả đúng shape, không được {}
        await ws.send(json.dumps({
            "jsonrpc": "2.0", "id": msg_id,
            "result": {"resources": []}
        }))


    elif method == "prompts/list":
        await ws.send(json.dumps({
            "jsonrpc": "2.0", "id": msg_id,
            "result": {"prompts": []}
        }))


    elif method == "tools/call":
        if msg_id is None:
            await ws.send(json.dumps({
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32600, "message": "Invalid Request: tools/call requires id"}
            }))
            return


        tool_name = msg.get("params", {}).get("name", "")
        arguments = msg.get("params", {}).get("arguments", {})
        if not isinstance(arguments, dict):
            arguments = {}
        print(f"[WS] Tool call: {tool_name}({arguments})")


        # Luôn dùng str làm key pending_results + JSON id (chuỗi) để R4 khớp POST /result
        cmd_id        = str(msg_id)
        asyncio_event = asyncio.Event()
        t_event       = threading.Event()


        with lock:
            pending_results[cmd_id] = {
                "asyncio_event": asyncio_event,
                "thread_event":  t_event,
                "result": {}
            }
            pending_commands.append({
                "id": cmd_id, "tool": tool_name,
                "args": arguments, "action": "execute"
            })


        try:
            await asyncio.wait_for(asyncio_event.wait(), timeout=20.0)
            with lock:
                r = pending_results.pop(cmd_id, {}).get("result", {"success": True})
        except asyncio.TimeoutError:
            with lock:
                pending_results.pop(cmd_id, None)
                pending_commands[:] = [c for c in pending_commands if c["id"] != cmd_id]
            r = {"success": False, "error": "R4 timeout"}
            print(f"[WS] TIMEOUT: {tool_name}")

        # Dong bo trang thai sau khi Xiaozhi ra lenh thanh cong
        if r.get("success") or "error" not in r:
            _sync_device_state(tool_name, arguments)

        await ws.send(json.dumps({
            "jsonrpc": "2.0", "id": msg_id,
            "result": {
                "content": [{"type": "text", "text": json.dumps(r, ensure_ascii=False)}],
                "isError": not r.get("success", True)
            }
        }))


    elif method and msg_id is not None:
        await ws.send(json.dumps({"jsonrpc": "2.0", "id": msg_id, "result": {}}))


async def connect_loop():
    global main_loop
    main_loop = asyncio.get_running_loop()
    while True:
        try:
            print(f"[WS] Dang ket noi {MCP_ENDPOINT[:60]}...")
            ssl_ctx = ssl.create_default_context()
            async with websockets.connect(MCP_ENDPOINT, ssl=ssl_ctx) as ws:
                await handle_mcp(ws)
        except Exception as e:
            print(f"[WS] Loi: {e}")
        print("[WS] Thu lai sau 5 giay...")
        await asyncio.sleep(5)


# =====================================================================
# MAIN
# =====================================================================
if __name__ == "__main__":
    threading.Thread(target=run_flask, daemon=True).start()
    threading.Thread(target=background_sync_r4, daemon=True).start()


    print("=" * 50)
    print("MCP Bridge v4.0 - Den + Quat only")
    print(f"HTTP: http://0.0.0.0:{HTTP_PORT}")
    print("=" * 50)


    asyncio.run(connect_loop())




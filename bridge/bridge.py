"""QuestPhone bridge (Windows). Phone <-USB(usbmuxd)-> this script <-UDP-> SteamVR driver.
Needs iTunes (Apple Mobile Device Support) installed and the phone plugged in + trusted."""
import plistlib, socket, struct, threading, time
import cv2, dxcam

MUX, PORT, DRIVER = ("127.0.0.1", 27015), 5555, ("127.0.0.1", 5601)
REGION = (0, 0, 1920, 888)  # SteamVR window drawn by the driver
# model byte from phone: 0=Quest 2, 1=Quest 3, 2=Quest 3S -> (width, height, jpeg quality)
MODELS = {0: (1280, 592, 70), 1: (1600, 740, 75), 2: (1280, 592, 70)}


def recvn(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise ConnectionError("closed")
        b += c
    return b


def mux(s, msg):
    msg.update(ClientVersionString="questphone", ProgName="questphone")
    p = plistlib.dumps(msg)
    s.sendall(struct.pack("<IIII", 16 + len(p), 1, 8, 1) + p)
    n = struct.unpack("<I", recvn(s, 16)[:4])[0]
    return plistlib.loads(recvn(s, n - 16))


def connect_phone():
    while True:
        try:
            s = socket.create_connection(MUX, timeout=3)
            devs = mux(s, {"MessageType": "ListDevices"}).get("DeviceList", [])
            s.close()
            if devs:
                s = socket.create_connection(MUX, timeout=3)
                r = mux(s, {"MessageType": "Connect", "DeviceID": devs[0]["DeviceID"],
                            "PortNumber": socket.htons(PORT)})
                if r.get("Number") == 0:
                    s.settimeout(None)
                    return s
                s.close()
                print("Phone found but app not listening - open QuestPhone and pick a model")
            else:
                print("Waiting for iPhone over USB...")
        except OSError as e:
            print("usbmuxd not reachable (is iTunes installed?):", e)
        time.sleep(2)


def send(s, t, payload):
    s.sendall(struct.pack(">BI", t, len(payload)) + payload)


def session(cam):
    s = connect_phone()
    print("Phone connected")
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    model = [1]

    def reader():
        try:
            while True:
                t, n = struct.unpack(">BI", recvn(s, 5))
                p = recvn(s, n)
                if t == 1 and n == 52:
                    udp.sendto(p, DRIVER)
                elif t == 3 and n == 1:
                    model[0] = p[0]
        except (ConnectionError, OSError):
            pass

    threading.Thread(target=reader, daemon=True).start()
    try:
        while True:
            f = cam.get_latest_frame()
            if f is None:
                time.sleep(0.005)
                continue
            w, h, q = MODELS.get(model[0], MODELS[1])
            f = cv2.resize(f, (w, h), interpolation=cv2.INTER_AREA)
            ok, jpg = cv2.imencode(".jpg", f, [cv2.IMWRITE_JPEG_QUALITY, q])
            if ok:
                send(s, 2, jpg.tobytes())
    except OSError:
        print("Phone disconnected")
    finally:
        s.close()


def main():
    cam = dxcam.create(output_color="BGR")
    cam.start(region=REGION, target_fps=60)
    while True:
        session(cam)


if __name__ == "__main__":
    main()

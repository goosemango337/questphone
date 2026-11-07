import Network
import UIKit

/// TCP server on port 5555. The PC connects through USB (usbmuxd).
/// Frame: [type u8][length u32 big-endian][payload]. 1=state(phone->PC) 2=jpeg(PC->phone) 3=model(phone->PC)
final class Link: ObservableObject {
    @Published var frame: UIImage?
    @Published var connected = false
    var model: UInt8 = 1
    private var listener: NWListener?
    private var conn: NWConnection?
    private var buf = [UInt8]()

    func start() {
        guard listener == nil, let l = try? NWListener(using: .tcp, on: 5555) else { return }
        l.newConnectionHandler = { [weak self] c in self?.accept(c) }
        l.start(queue: .main)
        listener = l
    }

    private func accept(_ c: NWConnection) {
        conn?.cancel()
        conn = c
        buf.removeAll()
        c.stateUpdateHandler = { [weak self] s in
            if case .ready = s { self?.connected = true; self?.send(3, Data([self?.model ?? 1])) }
            if case .failed = s { self?.connected = false }
            if case .cancelled = s { self?.connected = false }
        }
        c.start(queue: .main)
        read(c)
    }

    private func read(_ c: NWConnection) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 1 << 20) { [weak self] d, _, done, err in
            guard let self = self, c === self.conn else { return }
            if let d = d { self.buf.append(contentsOf: d); self.parse() }
            if err == nil && !done { self.read(c) } else { self.connected = false }
        }
    }

    private func parse() {
        while buf.count >= 5 {
            let n = Int(buf[1]) << 24 | Int(buf[2]) << 16 | Int(buf[3]) << 8 | Int(buf[4])
            if buf.count < 5 + n { return }
            if buf[0] == 2, let img = UIImage(data: Data(buf[5..<(5 + n)])) { frame = img }
            buf.removeFirst(5 + n)
        }
    }

    func send(_ type: UInt8, _ payload: Data) {
        guard let c = conn, c.state == .ready else { return }
        var h = Data([type])
        var n = UInt32(payload.count).bigEndian
        h.append(Data(bytes: &n, count: 4))
        c.send(content: h + payload, completion: .idempotent)
    }
}

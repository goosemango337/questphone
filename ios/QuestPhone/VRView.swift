import SwiftUI

final class Controls: ObservableObject {
    @Published var ls = SIMD2<Float>(0, 0)
    @Published var rs = SIMD2<Float>(0, 0)
    var lt: Float = 0, rt: Float = 0, lg: Float = 0, rg: Float = 0
    var buttons: UInt32 = 0  // 1=A 2=B 4=X 8=Y 16=Menu 32=System
    var aimYaw: Float = 0, aimPitch: Float = 0
    var last = CGSize.zero
    func set(_ bit: UInt32, _ on: Bool) { buttons = on ? buttons | bit : buttons & ~bit }
}

struct VRView: View {
    @ObservedObject var link: Link
    let model: Int
    @StateObject private var c = Controls()
    @StateObject private var head = Head()
    private let tick = Timer.publish(every: 1.0 / 60, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            Color.black.contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        c.aimYaw -= Float(g.translation.width - c.last.width) * 0.004
                        c.aimPitch = max(-1.2, min(1.2, c.aimPitch - Float(g.translation.height - c.last.height) * 0.004))
                        c.last = g.translation
                    }
                    .onEnded { _ in c.last = .zero })
            if let img = link.frame {
                Image(uiImage: img).resizable().scaledToFill().allowsHitTesting(false)
            } else {
                Text(link.connected ? "Start SteamVR on your PC" : "Waiting for PC (run bridge.py)")
                    .foregroundColor(.gray).allowsHitTesting(false)
            }
            VStack {
                HStack {
                    Pad("Menu") { c.set(16, $0) }
                    Pad("Center") { if $0 { head.recenter(); c.aimYaw = 0; c.aimPitch = 0 } }
                    Pad("Sys") { c.set(32, $0) }
                }
                Spacer()
                HStack(alignment: .bottom) {
                    VStack(spacing: 8) {
                        HStack { Pad("LT") { c.lt = $0 ? 1 : 0 }; Pad("LG") { c.lg = $0 ? 1 : 0 } }
                        HStack { Pad("X") { c.set(4, $0) }; Pad("Y") { c.set(8, $0) } }
                        Stick(v: $c.ls)
                    }
                    Spacer()
                    VStack(spacing: 8) {
                        HStack { Pad("RG") { c.rg = $0 ? 1 : 0 }; Pad("RT") { c.rt = $0 ? 1 : 0 } }
                        HStack { Pad("A") { c.set(1, $0) }; Pad("B") { c.set(2, $0) } }
                        Stick(v: $c.rs)
                    }
                }
            }
            .padding(20)
        }
        .ignoresSafeArea()
        .onAppear { head.start() }
        .onReceive(tick) { _ in link.send(1, packet()) }
    }

    private func packet() -> Data {
        let q = head.q.vector
        var f: [Float] = [q.x, q.y, q.z, q.w, c.ls.x, c.ls.y, c.rs.x, c.rs.y, c.lt, c.rt, c.lg, c.rg]
        // buttons then aim (below)
        var d = f.withUnsafeMutableBytes { Data($0) }
        var b = c.buttons
        d.append(Data(bytes: &b, count: 4))
        var aim: [Float] = [c.aimYaw, c.aimPitch]
        d.append(aim.withUnsafeMutableBytes { Data($0) })
        return d  // 60 bytes
    }
}

struct Pad: View {
    let label: String
    let onChange: (Bool) -> Void
    init(_ label: String, _ onChange: @escaping (Bool) -> Void) { self.label = label; self.onChange = onChange }
    var body: some View {
        Text(label).font(.caption.bold()).foregroundColor(.white)
            .frame(width: 50, height: 50)
            .background(Circle().fill(Color.white.opacity(0.28)))
            .gesture(DragGesture(minimumDistance: 0).onChanged { _ in onChange(true) }.onEnded { _ in onChange(false) })
    }
}

struct Stick: View {
    @Binding var v: SIMD2<Float>
    var body: some View {
        ZStack {
            Circle().fill(Color.white.opacity(0.15))
            Circle().fill(Color.white.opacity(0.45)).frame(width: 54, height: 54)
                .offset(x: CGFloat(v.x) * 46, y: CGFloat(-v.y) * 46)
        }
        .frame(width: 140, height: 140)
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { g in
                let x = max(-1, min(1, Float((g.location.x - 70) / 46)))
                let y = max(-1, min(1, Float(-(g.location.y - 70) / 46)))
                v = SIMD2(x, y)
            }
            .onEnded { _ in v = SIMD2(0, 0) })
    }
}

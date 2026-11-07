import SwiftUI

@main
struct QuestPhoneApp: App {
    var body: some Scene { WindowGroup { ContentView() } }
}

struct ContentView: View {
    @StateObject private var link = Link()
    @State private var model: Int? = nil
    private let names = ["Quest 2", "Quest 3", "Quest 3S"]

    var body: some View {
        Group {
            if let m = model {
                VRView(link: link, model: m)
            } else {
                ZStack {
                    Color.black.ignoresSafeArea()
                    VStack(spacing: 14) {
                        Text("Pick your headset").font(.title2.bold()).foregroundColor(.white)
                        HStack(spacing: 14) {
                            ForEach(0..<3, id: \.self) { i in
                                Button(names[i]) { link.model = UInt8(i); model = i }
                                    .padding(.horizontal, 22).padding(.vertical, 14)
                                    .background(Color.blue).foregroundColor(.white).cornerRadius(12)
                            }
                        }
                        Text(link.connected ? "PC connected" : "Waiting for PC (run bridge.py)")
                            .foregroundColor(link.connected ? .green : .gray)
                    }
                }
            }
        }
        .statusBar(hidden: true)
        .onAppear { link.start(); UIApplication.shared.isIdleTimerDisabled = true }
    }
}

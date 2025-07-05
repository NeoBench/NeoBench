use eframe::{egui, epi};
use neofs::fs::header::NeoFsHeader;

struct App { header: Option<NeoFsHeader> }

impl Default for App { fn default() -> Self { Self { header: None } } }

impl epi::App for App {
    fn name(&self) -> &str { "NeoBench GUI" }
    fn update(&mut self, ctx: &egui::Context, _frame: &epi::Frame) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("NeoBench ROM Viewer");
            if ui.button("Load roms/neokick.rom").clicked() {
                if let Ok(buf) = std::fs::read("roms/neokick.rom") {
                    self.header = NeoFsHeader::from_bytes(&buf);
                }
            }
            if let Some(h) = &self.header {
                ui.separator();
                ui.label(format!("Magic:       {:?}", h.magic));
                ui.label(format!("Total blocks: {}", h.total_blocks));
                ui.label(format!("Volume name: {}",
                    String::from_utf8_lossy(&h.volume_name)));
            }
        });
    }
}

fn main() {
    let opts = eframe::NativeOptions::default();
    eframe::run_native(Box::new(App::default()), opts);
}

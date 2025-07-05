
use clap::{Parser, Subcommand};
use anyhow::Result;
use neofs::fs::header::NeoFsHeader;

#[derive(Parser)]
#[command(name="neocli", version, about="NeoBench CLI")]
struct Cli {
#[command(subcommand)]
cmd: Commands
}

#[derive(Subcommand)]
enum Commands {
List,
Dump { path: String },
}

fn main() -> Result<()> {
let cli = Cli::parse();
match cli.cmd {
Commands::List => {
println!("Available ROMs:");
for entry in std::fs::read_dir("../roms")? {
let p = entry?.path();
if p.extension().map(|e| e == "rom").unwrap_or(false) {
println!(" {}", p.display());
}
}
}
Commands::Dump { path } => {
let buf = std::fs::read(&path)?;
if let Some(hdr) = NeoFsHeader::from_bytes(&buf) {
// avoid unaligned borrow by destructuring
let NeoFsHeader { magic, total_blocks, volume_name, .. } = hdr;
println!("Magic: {:?}", magic);
println!("Total blocks: {}", total_blocks);
println!("Volume name: {}", String::from_utf8_lossy(&volume_name));
} else {
println!("Not a valid NeoFS header.");
}
}
}
Ok(())
}

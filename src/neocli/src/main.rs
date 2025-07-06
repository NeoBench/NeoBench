use clap::{Parser, Subcommand};
use anyhow::Result;
use neofs::fs::header::NeoFsHeader;

#[derive(Parser)]
#[command(name="neocli", version)]
struct Cli {
    #[command(subcommand)]
    cmd: Commands,
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
            for entry in std::fs::read_dir("roms")? {
                let p = entry?.path();
                if p.extension().map(|e| e == "rom").unwrap_or(false) {
                    println!("  {}", p.display());
                }
            }
        }
        Commands::Dump { path } => {
            let buf = std::fs::read(&path)?;
            if let Some(hdr) = NeoFsHeader::from_bytes(&buf) {
                // copy unaligned field out to local
                let total_blocks = hdr.total_blocks;
                println!("Magic:       {:?}", hdr.magic);
                println!("Total blocks: {}", total_blocks);
            } else {
                println!("Not a valid NeoFS header.");
            }
        }
    }
    Ok(())
}

use rustyline::Editor;
use anyhow::Result;
use neofs::fs::header::NeoFsHeader;

fn main() -> Result<()> {
    let mut rl = Editor::<()>::new()?;
    loop {
        let line = rl.readline("neoshell> ")?;
        match line.as_str() {
            "list" => {
                for e in std::fs::read_dir("roms")? {
                    let p = e?.path();
                    if p.extension().map(|e| e == "rom").unwrap_or(false) {
                        println!("{}", p.display());
                    }
                }
            }
            cmd if cmd.starts_with("dump ") => {
                let path = cmd.split_whitespace().nth(1).unwrap();
                let buf = std::fs::read(path)?;
                if let Some(hdr) = NeoFsHeader::from_bytes(&buf) {
                    let total = hdr.total_blocks;
                    println!("Magic:        {:?}", hdr.magic);
                    println!("Total blocks: {}", total);
                } else {
                    println!("Invalid NeoFS header.");
                }
            }
            "exit" => break,
            _ => println!("Unknown command"),
        }
    }
    Ok(())
}

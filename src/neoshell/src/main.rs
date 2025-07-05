use anyhow::Result;
use clap::Parser;
use rustyline::error::ReadlineError;
use rustyline::Editor;
use neofs::fs::header::NeoFsHeader;

// this comes from your `src/neofs/src/lib.rs`
extern "C" { fn neofs_init(); }

fn initialize_fs() {
    unsafe { neofs_init() };
}

let mut rl = Editor::<()>::new()?;
if let Some(script) = std::env::args().nth(1) {
  for line in std::fs::read_to_string(script)?.lines() {
    if !handle_command(line)? { return Ok(()); }
  }
}

loop {
  match rl.readline("neoshell> ") {
    Ok(line) => {
      rl.add_history_entry(line.as_str());
      if !handle_command(&line)? {
        break;
      }
    }
    Err(ReadlineError::Interrupted) | Err(ReadlineError::Eof) => break,
    Err(e) => {
      eprintln!("Error: {:?}", e);
      break;
    }
  }
}

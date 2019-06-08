# InteractiveAssembler64

#inasm64

This project is Work-In-Progress, but the end goal is pretty clear; a simple command line interactive IA 64 assembler inspired by the old DOS debug.exe.

## Why?
Because I think it's nice to be able to just write some lines of assembly and see them execute immediately. There are a lot of  instructions available on modern CPUs and being able to try them out and see the effect interactively is nice. 
It's also an interesting project because it involves writing a small debugger using the Windows Debug APIs, and interacting with an assembler. 

## When?
It's a pet project, and with a full time job and two kids it's not going to be finished quickly!
If you're interested then please come back occasionally to check up on progress, I'll be using twitter @psjarlo to post updates now and then too.

## Dependencies
### Intel XED https://github.com/intelxed/xed
Currently expects it to be built and present in "xed" folder one up relative to the inasm64 solution, i.e. it is referenced as ../xed/


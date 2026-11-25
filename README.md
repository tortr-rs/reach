reach
=====

ssh by name on the local network. reach finds the machine, puts your key there the first time (ssh asks the
password once) and logs in; ssh does the connecting.

    reach start [NAME]   answer to NAME (the host name) on this network, sshd started if needed
    reach stop           stop answering
    reach list           what answers on this network
    reach NAME [CMD]     log in to NAME, or run CMD there

    ./build.sh           builds it on c.h, ./build.sh 32 for one i386 binary
    cc -DLIBC -O2 -o reach reach.c    on a libc

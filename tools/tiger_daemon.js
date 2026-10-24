const readline = require('readline');
const rl = readline.createInterface({ input: process.stdin, terminal: false });

let globalTIGER = {};
globalTIGER.imprimir = console.log;
globalTIGER.print = console.log;

let lines = [];
let collecting = false;

process.stdout.write("__TIGER_READY__\n");

rl.on('line', (line) => {
    if (line === '__TIGER_REQ_START__') {
        collecting = true;
        lines = [];
        globalTIGER = {};
        globalTIGER.imprimir = console.log;
        globalTIGER.print = console.log;
    } else if (line === '__TIGER_REQ_END__') {
        collecting = false;
        try {
            eval(lines.join('\n'));
        } catch (e) {
            console.error(e);
        }
        process.stdout.write("__TIGER_BATCH_DONE__\n");
    } else if (collecting) {
        lines.push(line);
    }
});


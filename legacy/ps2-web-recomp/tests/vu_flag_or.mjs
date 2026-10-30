import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import assert from 'node:assert/strict';

const dir = process.argv[2];
const layout = JSON.parse(readFileSync(join(dir, 'layout.json')));
const memory = new WebAssembly.Memory({ initial: 1, maximum: 65536, shared: true });
const view = new DataView(memory.buffer);
let mac, status, clip;
const unexpected = () => { throw new Error('unexpected synthetic-program helper'); };
const { instance } = await WebAssembly.instantiate(readFileSync(join(dir, 'program.wasm')), {
    env: { memory, mac: () => mac, status: () => status, clip: () => clip,
        force_mac: unexpected, efu: unexpected, kick: unexpected,
        top: unexpected, itop: unexpected, interpret: unexpected },
});
let cases = 0;
for (mac of [0, 0x20, 0x10, 0xffff, 0x12345678]) {
    for (status of [0, 0x20, 0x824, 0xfff, 0xabcde]) {
        for (const operand of [0, 0x10, 0x20, 0xffff]) {
            for (clip of [0, 0xffffff]) {
                new Uint8Array(memory.buffer).fill(0);
                view.setUint32(layout.mac, mac, true);
                view.setUint32(layout.status, status, true);
                view.setUint32(layout.clip, clip, true);
                view.setUint16(layout.vi + 2, operand, true);
                instance.exports.run(0, 8192);
                const vi = i => view.getUint16(layout.vi + i * 2, true);
                assert.equal(vi(2), (mac & 0xffff) | operand, 'FMOR');
                assert.equal(vi(3), (status & 0xfff) | 0x824, 'FSOR');
                assert.equal(vi(4), Number((mac & 0xffff) === operand), 'FMEQ');
                assert.equal(vi(5), Number((status & 0xfff) === 0x824), 'FSEQ');
                assert.equal(vi(6), (mac & 0xffff) & operand, 'FMAND');
                assert.equal(vi(7), (status & 0xfff) & 0x824, 'FSAND');
                assert.equal(vi(1), Number(clip === 0xffffff), 'FCOR');
                assert.equal(vi(0), 0, 'VI0');
                ++cases;
            }
        }
    }
}
console.log(`PASS: ${cases} flag cases through the emitted WebAssembly`);

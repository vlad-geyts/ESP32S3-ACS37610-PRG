"""G2/G3 hardware smoke test — exercises the full Python stack against the
real programmer, mirroring the GUI plan §8.2/§8.3 workflows.

Usage:  python hw_smoke.py COM5
        python hw_smoke.py           (lists available ports)

Sequence: *IDN? -> PWRON -> AUTH -> READ all six registers (decoded) -> PWROFF.
Read-only — no writes are performed.
"""
import sys

from acs_gui import registers
from acs_gui.protocol import ProtocolClient, ProtocolError
from acs_gui.transport import SerialTransport, TransportError, list_ports


def main() -> int:
    if len(sys.argv) < 2:
        print("Available ports:", ", ".join(list_ports()) or "(none)")
        print("Usage: python hw_smoke.py <COMx>")
        return 1

    transport = SerialTransport()
    transport.open(sys.argv[1])
    client = ProtocolClient(transport)

    try:
        print("IDN     :", client.idn())
        print("STATUS  :", client.status())

        client.power_on()
        print("PWRON   : OK")
        client.auth()
        print("AUTH    : OK (device port open)")

        for addr in registers.ALL_ADDRS:
            r = client.read_register(addr)
            reg = registers.BY_ADDR[addr]
            kind = "shadow" if addr == reg.shadow_addr else reg.access
            print(f"READ {addr:02X} : DATA=0x{r.data:08X} ECC={r.ecc}"
                  f"  [{reg.name} {kind}]")
            for name, value in registers.decode(addr, r.data).items():
                print(f"          {name} = {value}")

        client.power_off()
        print("PWROFF  : OK")
        print("\nG2/G3 smoke test PASSED")
        return 0
    except (ProtocolError, TransportError) as exc:
        print(f"\nFAILED: {type(exc).__name__}: {exc}")
        return 2
    finally:
        transport.close()


if __name__ == "__main__":
    sys.exit(main())

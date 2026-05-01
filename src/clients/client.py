import sys
from commands import Client


def read_input(client: Client):
    while True:
        curr_line = str(input("c> "))

        curr_command = curr_line.split(' ', 3)
        if curr_command[0] == "QUIT":
            client.quit()

        elif curr_command[0] == "REGISTER":
            if len(curr_command) != 2:
                print("INVALID COMMAND: REGISTER <userName>", file=sys.stderr)
                continue

            client.register(curr_command[1])

        elif curr_command[0] == "UNREGISTER":
            if len(curr_command) != 2:
                print("INVALID COMMAND: UNREGISTER <userName>", file=sys.stderr)
                continue

            client.unregister(curr_command[1])

        elif curr_command[0] == "CONNECT":
            if len(curr_command) != 2:
                print("INVALID COMMAND: CONNECT <userName>", file=sys.stderr)
                continue

            client.connect(curr_command[1])

        elif curr_command[0] == "DISCONNECT":
            if len(curr_command) != 2:
                print("INVALID COMMAND: DISCONNECT <userName>", file=sys.stderr)
                continue

            client.disconnect(curr_command[1])

        elif curr_command[0] == "SEND":
            if len(curr_command) < 3:
                print("INVALID COMMAND: SEND <userName> <message>", file=sys.stderr)
                continue

            client.send(curr_command[1], curr_command[2])

        #elif curr_command[0] == "SENDATTACH":
            #if len(curr_command) < 3:
            #    print("INVALID COMMAND: SENDATTACH <userName> <message>", file=sys.stderr)
            #    continue
            #client.send_attach()

        elif curr_command[0] == "USERS":
            client.users()

def parse_args():
    if len(sys.argv) != 5:
        print("Uso incorrecto del cliente:"
              "\n\t-s: Dirección del servidor"
              "\n\t-p: Puerto del servidor")
        sys.exit(-1)

    address, port = "", 0

    for i, arg in enumerate(sys.argv):
        if arg == "-s" and i < len(sys.argv):
            address = sys.argv[i+1]
        elif arg == "-p" and i < len(sys.argv):
            port = int(sys.argv[i+1])

    return address, port


if __name__ == "__main__":
    client_address, client_port = parse_args()
    read_input(Client(address=client_address, port=client_port))
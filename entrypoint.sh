#! /bin/sh
python ws.py > /dev/null 2>&1 &
python client.py -s "$SERVER_IP" -p "$SERVER_PORT"

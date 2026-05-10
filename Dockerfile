FROM python:3.11-slim

WORKDIR /app

ENV SERVER_IP="127.0.0.1"
ENV SERVER_PORT="8080"

COPY src/clients/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY src/clients/ .
COPY src/ws/ .
COPY entrypoint.sh .

RUN chmod +x entrypoint.sh

ENTRYPOINT ["./entrypoint.sh"]

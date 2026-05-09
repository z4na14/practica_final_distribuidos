from flask import Flask, request

app = Flask(__name__)


@app.route('/quitar-espacios', methods=['POST'])
def quitar_espacios():
    try:
        req = request.get_json()
        item = req['cadena']
        palabras_sin_espacios = item.split()
        item = " ".join(palabras_sin_espacios)
        return item, 200
    except Exception as e:
        return {"error": str(e)}, 415


app.run(debug=False, host="0.0.0.0", port=3000)

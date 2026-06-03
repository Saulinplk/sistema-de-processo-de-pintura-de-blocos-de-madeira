"""
Backend - Camada de Serviço
Recebe dados do ESP32 de monitoramento e salva no Supabase.

Como rodar:
    pip install -r requirements.txt
    python main.py
"""

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from supabase import create_client, Client
from datetime import datetime, timezone
import uvicorn
import os

# =====================================================
#   CONFIGURAÇÕES — edite aqui com seus dados Supabase
# =====================================================
SUPABASE_URL = "https://augzulogqvewkqqssjus.supabase.co"
SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImF1Z3p1bG9ncXZld2txcXNzanVzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3Nzk4OTg2MzcsImV4cCI6MjA5NTQ3NDYzN30.44wY-pGzHvryrsPRJOu-yEVDgtm6jPwfkVwzJPQNZK4"  # anon public key

# =====================================================
#   CLIENTE SUPABASE
# =====================================================
supabase: Client = create_client(SUPABASE_URL, SUPABASE_KEY)

# =====================================================
#   APP FASTAPI
# =====================================================
app = FastAPI(
    title="Backend IoT - Camada de Serviço",
    description="Recebe leituras do ESP32 e persiste no Supabase",
    version="1.0.0"
)

# =====================================================
#   MODELO DE DADOS
# =====================================================
class DadosSensores(BaseModel):
    nivel_tinta:  float
    temperatura:  float
    umidade:      float
    luminosidade: int
    presenca:     int          # 0 ou 1
    timestamp:    str | None = None   # ISO 8601 vindo do ESP32


# =====================================================
#   ROTAS
# =====================================================

@app.get("/")
def root():
    return {"status": "online", "hora_servidor": datetime.now(timezone.utc).isoformat()}


@app.post("/dados", status_code=201)
def receber_dados(dados: DadosSensores):
    """Recebe um pacote de sensores e insere no Supabase."""

    # Usa timestamp do ESP32 ou gera um novo
    ts = dados.timestamp if dados.timestamp else datetime.now(timezone.utc).isoformat()

    registro = {
        "nivel_tinta":  dados.nivel_tinta,
        "temperatura":  dados.temperatura,
        "umidade":      dados.umidade,
        "luminosidade": dados.luminosidade,
        "presenca":     bool(dados.presenca),
        "timestamp":    ts,
    }

    print(f"[DADOS] {registro}")

    try:
        resultado = supabase.table("sensor_readings").insert(registro).execute()
        id_inserido = resultado.data[0]["id"] if resultado.data else None
        print(f"[SUPABASE] Inserido com ID: {id_inserido}")
        return {"status": "ok", "id": id_inserido, "timestamp": ts}

    except Exception as e:
        print(f"[ERRO] Supabase: {e}")
        raise HTTPException(status_code=500, detail=f"Erro ao salvar: {str(e)}")


@app.get("/dados")
def listar_dados(limit: int = 50):
    """Retorna os últimos registros (útil para debug)."""
    try:
        resultado = (
            supabase.table("sensor_readings")
            .select("*")
            .order("timestamp", desc=True)
            .limit(limit)
            .execute()
        )
        return {"total": len(resultado.data), "registros": resultado.data}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


# =====================================================
#   INICIAR SERVIDOR
# =====================================================
if __name__ == "__main__":
    print("=" * 50)
    print("  Backend IoT - Camada de Serviço")
    print("  Supabase URL:", SUPABASE_URL)
    print("  Acesse: http://0.0.0.0:8000")
    print("  Docs:   http://0.0.0.0:8000/docs")
    print("=" * 50)
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)

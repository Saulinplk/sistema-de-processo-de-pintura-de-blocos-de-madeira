"""
Backend - Camada de Serviço
Sistema IoT - Processo de Pintura de Blocos de Madeira
 
Recebe os dados do ESP32 de monitoramento via HTTP e persiste no Supabase.
 
Como rodar:
    pip install -r requirements.txt
    python main.py
 
Depois acesse http://localhost:8000/docs para testar manualmente.
"""
 
import socket
from datetime import datetime, timezone
 
import uvicorn
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field
from supabase import create_client, Client
 
 
# =====================================================
#   CONFIGURAÇÕES — edite aqui com seus dados Supabase
# =====================================================
# Project URL e anon public key (Supabase -> Settings -> API)
SUPABASE_URL = "https://XXXXXXXXXXXXX.supabase.co"
SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
 
TABELA = "sensor_readings"   # nome da tabela criada no Supabase
PORTA = 8000
 
 
# =====================================================
#   CLIENTE SUPABASE (inicialização resiliente)
# =====================================================
# Se as credenciais ainda forem placeholder, o servidor sobe mesmo assim
# para você testar o formato da API. Só os inserts é que vão avisar.
supabase: Client | None = None
SUPABASE_CONFIGURADO = "XXXXX" not in SUPABASE_URL
 
if SUPABASE_CONFIGURADO:
    try:
        supabase = create_client(SUPABASE_URL, SUPABASE_KEY)
    except Exception as e:
        print(f"[AVISO] Não foi possível conectar ao Supabase: {e}")
        SUPABASE_CONFIGURADO = False
else:
    print("[AVISO] SUPABASE_URL/KEY ainda são placeholder. "
          "Os dados NÃO serão salvos até você configurar.")
 
 
# =====================================================
#   APP FASTAPI
# =====================================================
app = FastAPI(
    title="Backend IoT - Pintura de Blocos de Madeira",
    description="Recebe leituras do ESP32 e persiste no Supabase",
    version="1.0.0",
)
 
 
# =====================================================
#   MODELO DE DADOS
# =====================================================
class DadosSensores(BaseModel):
    nivel_tinta:  float
    temperatura:  float
    umidade:      float
    luminosidade: int
    presenca:     int = Field(ge=0, le=1)   # aceita só 0 ou 1
    # millis() do ESP (uptime, não é hora real). Opcional.
    timestamp:    int | None = None
 
 
# =====================================================
#   ROTAS
# =====================================================
@app.get("/")
def root():
    """Healthcheck simples."""
    return {
        "status": "online",
        "supabase": "configurado" if SUPABASE_CONFIGURADO else "NAO configurado",
        "hora_servidor": datetime.now(timezone.utc).isoformat(),
    }
 
 
@app.post("/dados", status_code=201)
def receber_dados(dados: DadosSensores):
    """Recebe um pacote de sensores e insere no Supabase."""
    # A hora real é sempre carimbada aqui no servidor.
    # O timestamp que vem do ESP é só o uptime e fica ignorado.
    ts = datetime.now(timezone.utc).isoformat()
 
    registro = {
        "nivel_tinta":  dados.nivel_tinta,
        "temperatura":  dados.temperatura,
        "umidade":      dados.umidade,
        "luminosidade": dados.luminosidade,
        "presenca":     bool(dados.presenca),
        "timestamp":    ts,
    }
    print(f"[DADOS] {registro}")
 
    if not SUPABASE_CONFIGURADO:
        # Servidor de teste: confirma o recebimento sem salvar.
        print("[AVISO] Recebido mas NÃO salvo (Supabase não configurado).")
        return {"status": "recebido_sem_salvar", "timestamp": ts}
 
    try:
        resultado = supabase.table(TABELA).insert(registro).execute()
        id_inserido = resultado.data[0]["id"] if resultado.data else None
        print(f"[SUPABASE] Inserido com ID: {id_inserido}")
        return {"status": "ok", "id": id_inserido, "timestamp": ts}
    except Exception as e:
        print(f"[ERRO] Supabase: {e}")
        raise HTTPException(status_code=500, detail=f"Erro ao salvar: {e}")
 
 
@app.get("/dados")
def listar_dados(limit: int = 50):
    """Retorna os últimos registros (útil para debug)."""
    if not SUPABASE_CONFIGURADO:
        raise HTTPException(status_code=503, detail="Supabase não configurado.")
    try:
        resultado = (
            supabase.table(TABELA)
            .select("*")
            .order("timestamp", desc=True)
            .limit(limit)
            .execute()
        )
        return {"total": len(resultado.data), "registros": resultado.data}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
 
 
# =====================================================
#   UTILITÁRIO — descobrir o IP local (o que vai no ESP32)
# =====================================================
def descobrir_ip_local() -> str:
    """Descobre o IP da máquina na rede local. NÃO conecta de fato,
    só usa o socket para escolher a interface de saída."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip
 
 
# =====================================================
#   INICIAR SERVIDOR
# =====================================================
if __name__ == "__main__":
    ip = descobrir_ip_local()
    print("=" * 60)
    print("  Backend IoT - Pintura de Blocos de Madeira")
    print(f"  Supabase: {'configurado' if SUPABASE_CONFIGURADO else 'NAO configurado'}")
    print()
    print(f"  >>> No ESP32 use:  http://{ip}:{PORTA}/dados <<<")
    print(f"  Local (neste PC):  http://127.0.0.1:{PORTA}")
    print(f"  Swagger / testes:  http://{ip}:{PORTA}/docs")
    print("=" * 60)
    uvicorn.run("main:app", host="0.0.0.0", port=PORTA, reload=True)

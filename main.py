from fastapi import FastAPI
from dotenv import load_dotenv
import os
import logging
from apscheduler.schedulers.asyncio import AsyncIOScheduler
from apscheduler.triggers.interval import IntervalTrigger
from src.routers.data_router import router as data_router
from src.services.data_service import DataService
from src.repositories.thingspeak_repository import ThingSpeakRepository

# Configuração básica de log do scheduler
logging.basicConfig(level=logging.INFO)

# Carrega as variáveis de ambiente
load_dotenv()

# Cria a instância principal do FastAPI
app = FastAPI(
    title="ThingSpeak-MongoDB Atlas API (MVC/SOLID) - c/ Agendamento",
    description="API de backend para coleta e disponibilização de dados do ThingSpeak para o MongoDB Atlas.",
    version="1.0.0"
)

# Inclui o roteador de dados
app.include_router(data_router)

# --- Funções do Scheduler ---

# O job que será executado automaticamente
def scheduled_data_fetch_job():
    """
    Função wrapper para a tarefa agendada.
    Cria novas instâncias de Repositório e Serviço para cada execução,
    garantindo que não haja problemas de concorrência ou estado (Princípio S).
    """
    print("\n--- INICIANDO COLETA AUTOMÁTICA DE DADOS ---")
    try:
        # Acesso direto à lógica de negócio, ignorando a camada de roteamento HTTP
        repository = ThingSpeakRepository()
        service = DataService(repository=repository)
        result = service.fetch_and_save_data()
        
        # Imprime o resultado da operação no console
        print(f"COLETA CONCLUÍDA: Status: {result['status']} | Mensagem: {result['message']}")
        
    except EnvironmentError as e:
        print(f"ERRO DE AMBIENTE (Coleta Agendada): {e}. Verifique seu .env.")
    except Exception as e:
        print(f"ERRO CRÍTICO na coleta agendada: {e}")
    print("--- FIM DA COLETA AUTOMÁTICA ---")


# Inicializa o scheduler
scheduler = AsyncIOScheduler()


# --- Eventos de Startup/Shutdown do FastAPI ---

@app.on_event("startup")
async def startup_event():
    """
    Executado quando o servidor FastAPI inicia.
    """
    print("Iniciando Agendador de Tarefas...")
    
    # Adiciona a tarefa: executa a cada 5 minutos
    # Ajuste 'minutes=5' para a frequência desejada (ex: seconds=30 ou hours=1)
    scheduler.add_job(
        scheduled_data_fetch_job, 
        trigger=IntervalTrigger(minutes=5), 
        id='thingspeak_fetch', 
        name='ThingSpeak Fetch Job', 
        replace_existing=True
    )
    
    # Inicia o agendador
    scheduler.start()
    print("Agendador iniciado! A coleta do ThingSpeak será executada a cada 5 minutos.")

@app.on_event("shutdown")
async def shutdown_event():
    """
    Executado quando o servidor FastAPI desliga.
    """
    print("Encerrando Agendador de Tarefas...")
    scheduler.shutdown()
    print("Agendador encerrado.")


# --- Endpoint de Saúde ---
@app.get("/", tags=["Saúde"])
def read_root():
    """Endpoint de saúde da API."""
    return {"message": "API está no ar! Acesse /docs para a documentação interativa."}

# --- Bloco de Execução (Uvicorn) ---
if __name__ == "__main__":
    import uvicorn
    
    # Pega as configurações do .env para rodar o servidor
    host = os.getenv("HOST", "0.0.0.0")
    port = int(os.getenv("PORT", 8000))
    
    # Inicia o servidor Uvicorn com hot-reload
    # O Uvicorn irá disparar os eventos de startup

    uvicorn.run("main:app", host=host, port=port, reload=True)

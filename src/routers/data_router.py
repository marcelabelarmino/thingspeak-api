from fastapi import APIRouter, Depends, HTTPException
from typing import List, Dict, Any

# Importa as classes que serão injetadas
from src.repositories.thingspeak_repository import ThingSpeakRepository
from src.services.data_service import DataService
from src.entities.data_entity import ThingSpeakData # Para documentação/resposta

router = APIRouter(prefix="/data", tags=["Dados ThingSpeak"])

# --- Injeção de Dependência ---
# Função que fornece uma nova instância do Repositório
def get_repository() -> ThingSpeakRepository:
    """Dependência para o Repositório."""
    return ThingSpeakRepository()

# Função que fornece uma nova instância do Serviço, injetando o Repositório
def get_service(repo: ThingSpeakRepository = Depends(get_repository)) -> DataService:
    """Dependência para o Serviço, injetando o Repositório."""
    return DataService(repository=repo)


# --- Rotas (Endpoints) ---

@router.post("/fetch-and-save", summary="Busca dados do ThingSpeak e salva no MongoDB")
async def fetch_and_save_data(
    service: DataService = Depends(get_service) # Injeta a dependência do serviço
):
    """
    Endpoint para disparar o processo principal:
    ThingSpeak > Backend API > MongoDB Atlas.
    """
    try:
        result = service.fetch_and_save_data()
        
        if result["status"] == "falha":
            raise HTTPException(status_code=400, detail=result["message"])
            
        return result
    except Exception as e:
        # Erro genérico (ex: problema de conexão com o MongoDB ou ThingSpeak)
        print(f"Erro fatal na rota /fetch-and-save: {e}")
        raise HTTPException(status_code=500, detail="Erro interno do servidor ao processar a requisição.")


@router.get("/all", response_model=List[ThingSpeakData], summary="Retorna todos os dados salvos do MongoDB")
async def get_all_data(
    service: DataService = Depends(get_service) # Injeta a dependência do serviço
) -> List[Dict[str, Any]]:
    """
    Endpoint para disponibilizar os dados salvos:
    MongoDB Atlas > Backend API > Rotas Front-End.
    """
    data = service.get_all_data()
    if not data:
        # Se não houver dados, retorna 404 para o Front-End
        raise HTTPException(status_code=404, detail="Nenhum dado encontrado no MongoDB.")
        

    return data

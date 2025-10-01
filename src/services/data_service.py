# src/services/data_service.py
from typing import List, Dict, Any
from datetime import datetime
from src.repositories.thingspeak_repository import ThingSpeakRepository
from src.entities.data_entity import ThingSpeakData

class DataService:
    """
    Camada de Serviço (Lógica de Negócio).
    Orquestra a busca, transformação e persistência dos dados.
    """
    
    def __init__(self, repository: ThingSpeakRepository):
        # Recebe o Repositório via Injeção de Dependência (Princípio D - Dependency Inversion)
        self.repo = repository

    def _transform_feeds_to_entities(self, feeds: List[Dict[str, Any]]) -> List[ThingSpeakData]:
        """
        Transforma a lista de feeds JSON do ThingSpeak em Entidades Pydantic.
        """
        entities = []
        for feed in feeds:
            try:
                # Transforma a string de data para um objeto datetime
                created_at = datetime.strptime(feed.get("created_at"), "%Y-%m-%dT%H:%M:%SZ")
                
                # Mapeia e valida os dados com a Entidade Pydantic
                entity = ThingSpeakData(
                    created_at=created_at,
                    field1=float(feed.get("field1")) if feed.get("field1") is not None else None,
                    field2=float(feed.get("field2")) if feed.get("field2") is not None else None,
                    # Adicione os outros fields aqui
                )
                entities.append(entity)
            except Exception as e:
                # Ignora feeds com dados inválidos ou incompletos (Princípio R - Robustez)
                print(f"Aviso: Feed ignorado devido a erro de transformação: {e} - Dados: {feed}")
                continue
        return entities

    def fetch_and_save_data(self) -> Dict[str, Any]:
        """
        1. Pega os dados mais recentes do ThingSpeak.
        2. Transforma em entidades válidas.
        3. Salva no MongoDB Atlas.
        """
        
        # 1. Pega os dados do ThingSpeak (API Externa)
        feeds = self.repo._fetch_latest_data()
        if not feeds:
            return {"status": "falha", "message": "Nenhum dado novo encontrado no ThingSpeak."}

        # 2. Transforma e Valida
        data_entities = self._transform_feeds_to_entities(feeds)
        if not data_entities:
             return {"status": "falha", "message": "Todos os feeds encontrados foram inválidos."}
        
        # 3. Salva no MongoDB
        count = self.repo.save_data_to_db(data_entities)

        return {
            "status": "sucesso",
            "message": f"{count} registros foram salvos no MongoDB.",
            "total_feeds_recebidos": len(feeds)
        }

    def get_all_data(self) -> List[Dict[str, Any]]:
        """
        Busca todos os dados salvos no MongoDB.
        """
        # Retorna os dados brutos do MongoDB
        raw_data = self.repo.find_all_data()
        
        # Converte o _id de ObjectId para string para JSON serialização
        for doc in raw_data:
            doc["_id"] = str(doc["_id"])
        
        return raw_data
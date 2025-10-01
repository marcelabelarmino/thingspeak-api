# src/repositories/thingspeak_repository.py (CONTEÚDO COMPLETO MODIFICADO)
import requests
import os
from typing import List, Dict, Any
from dotenv import load_dotenv
from src.config.database import get_collection
from src.entities.data_entity import ThingSpeakData
from pymongo import ASCENDING
from pymongo.errors import DuplicateKeyError

# Carrega as variáveis do .env
load_dotenv()

class ThingSpeakRepository:
    # ... (restante do __init__ inalterado)

    def __init__(self):
        # Configurações do ThingSpeak
        self.channel_id = os.getenv("THINGSPEAK_CHANNEL_ID")
        self.read_key = os.getenv("THINGSPEAK_READ_API_KEY")
        self.base_url = f"https://api.thingspeak.com/channels/{self.channel_id}/feeds.json"
        
        # Configuração do MongoDB
        self.collection = get_collection()

        if not self.channel_id or not self.read_key:
             raise EnvironmentError("Variáveis do ThingSpeak não encontradas no .env")

        # NOVO: Cria um índice único para o campo 'created_at'
        # Isso garante que se tentarmos salvar um dado com um 'created_at' duplicado,
        # o MongoDB lançará uma exceção, impedindo a duplicata.
        try:
            self.collection.create_index([("created_at", ASCENDING)], unique=True)
            print("Índice único em 'created_at' criado/verificado.")
        except Exception as e:
            # Em produção, você pode querer logar isso de forma mais robusta.
            print(f"Aviso: Não foi possível criar o índice único: {e}")


    # --- Métodos de Interação com a API Externa (ThingSpeak) ---

    # ... (método _fetch_latest_data inalterado)
    def _fetch_latest_data(self) -> List[Dict[str, Any]]:
        """
        Busca os feeds mais recentes do ThingSpeak.
        Pode ser ajustado para buscar um número específico de feeds (ex: ?results=10).
        """
        params = {
            "api_key": self.read_key,
            "results": 10 # Pega os 10 resultados mais recentes
        }
        
        try:
            response = requests.get(self.base_url, params=params, timeout=10)
            response.raise_for_status() # Lança um erro para códigos de status ruins (4xx ou 5xx)
            data = response.json()
            
            # Retorna apenas a lista de feeds (dados)
            return data.get("feeds", [])
        
        except requests.exceptions.RequestException as e:
            print(f"Erro ao buscar dados do ThingSpeak: {e}")
            return []


    # --- Métodos de Persistência (MongoDB) ---
    
    def save_data_to_db(self, data_list: List[ThingSpeakData]) -> int:
        """
        Salva uma lista de objetos ThingSpeakData no MongoDB, ignorando duplicatas.
        Retorna o número de documentos INSERIDOS com sucesso.
        """
        if not data_list:
            return 0
        
        # Converte a lista de entidades Pydantic para o formato de dicionário
        documents = [doc.model_dump(by_alias=True, exclude_none=True) for doc in data_list]
        
        # Se usarmos insert_many com dados duplicados, ele falha a operação inteira.
        # Para salvar o máximo possível e contar os salvos, usamos um loop com tratamento de erro.
        inserted_count = 0
        
        for doc in documents:
            try:
                # Tenta inserir um por um
                self.collection.insert_one(doc)
                inserted_count += 1
            except DuplicateKeyError:
                # Ignora a duplicata e continua para o próximo documento
                pass
            except Exception as e:
                print(f"Erro inesperado ao salvar um documento: {e}")
                
        return inserted_count

    # ... (método find_all_data inalterado)
    def find_all_data(self) -> List[Dict[str, Any]]:
        """
        Busca todos os documentos salvos na coleção.
        """
        # Limita a busca para simplificar o teste, pode ser removido em produção
        return list(self.collection.find().sort("created_at", -1).limit(50))
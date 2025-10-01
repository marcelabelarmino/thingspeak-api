# src/config/database.py
from pymongo import MongoClient
from dotenv import load_dotenv
import os

# Carrega as variáveis do arquivo .env
load_dotenv()

class MongoDB:
    """
    Classe de Singleton para gerenciar a conexão com o MongoDB.
    Garante que haverá apenas uma instância do cliente MongoDB (Princípio O - Open/Closed).
    """
    __instance = None

    @staticmethod
    def get_client():
        """
        Retorna o cliente MongoDB. Cria se ainda não existir.
        """
        if MongoDB.__instance is None:
            # Obtém as variáveis de conexão
            mongo_uri = os.getenv("MONGO_URI")
            if not mongo_uri:
                raise EnvironmentError("Variável MONGO_URI não encontrada no .env")
            
            # Cria e armazena a instância do cliente
            MongoDB.__instance = MongoClient(mongo_uri)
            print("Conexão com MongoDB estabelecida.")
            
        return MongoDB.__instance

# A função get_database facilita o acesso ao banco de dados específico
def get_database():
    """
    Retorna o banco de dados configurado no .env.
    """
    client = MongoDB.get_client()
    db_name = os.getenv("MONGO_DB_NAME")
    if not db_name:
        raise EnvironmentError("Variável MONGO_DB_NAME não encontrada no .env")
        
    return client[db_name]

# A função get_collection facilita o acesso à collection específica
def get_collection():
    """
    Retorna a collection configurada para os dados do ThingSpeak.
    """
    db = get_database()
    collection_name = os.getenv("MONGO_COLLECTION_NAME")
    if not collection_name:
        raise EnvironmentError("Variável MONGO_COLLECTION_NAME não encontrada no .env")
        
    return db[collection_name]
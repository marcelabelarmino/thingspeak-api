# src/entities/data_entity.py
from pydantic import BaseModel, Field
from datetime import datetime
from typing import Optional

class ThingSpeakData(BaseModel):
    """
    Define o esquema dos dados recebidos e salvos no MongoDB.
    """
    # Usado para o id gerado pelo MongoDB, opcional ao criar
    id: Optional[str] = Field(alias="_id", default=None)
    
    created_at: datetime = Field(..., description="Data e hora da criação do registro no ThingSpeak.")
    
    # Exemplo de campos (Field1 e Field2) - ajuste conforme seu Channel
    field1: Optional[float] = Field(None, description="Valor do Field 1 (Ex: Temperatura).")
    field2: Optional[float] = Field(None, description="Valor do Field 2 (Ex: Umidade).")
    
    # Você pode adicionar mais campos (field3, field4, etc.) conforme a sua necessidade.
    
    class Config:
        # Permite que a Pydantic mapeie o campo 'id' (str) para '_id' (ObjectID)
        populate_by_name = True
        json_encoders = {
            datetime: lambda dt: dt.isoformat(),
            # Exemplo: ObjectId: str  # Se você quiser retornar o _id como string
        }
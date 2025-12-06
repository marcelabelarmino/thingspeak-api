# 📊 ThingSpeak to MongoDB API

Uma API robusta desenvolvida em **FastAPI** para coletar, processar e armazenar dados do **ThingSpeak** no **MongoDB Atlas**, seguindo os princípios **SOLID** e arquitetura em camadas.

## 🚀 Funcionalidades

- **🔄 Coleta Automática**: Agendamento de coleta de dados do ThingSpeak a cada 5 minutos
- **💾 Persistência**: Armazenamento seguro no MongoDB Atlas com tratamento de duplicatas
- **📡 API REST**: Endpoints para consulta e manipulação dos dados
- **🔒 Validação**: Schema validation com Pydantic
- **📊 Monitoramento**: Logs detalhados e tratamento de erros
- **📚 Documentação**: API docs automática com Swagger UI

## 🏗️ Arquitetura

```
├── main.py             # Ponto de entrada da aplicação
├── requirements.txt    # Dependências do projeto
└── src/
    ├── config/         # Configurações gerais e conexão com o banco de dados
    ├── entities/       # Modelos de dados (Pydantic)
    ├── repositories/   # Camada de acesso a dados (CRUD, queries, DAO)
    ├── services/       # Regras de negócio e lógica da aplicação
    ├── routers/        # Endpoints e rotas da API (FastAPI)
```

## 📋 Pré-requisitos

- Python 3.8+
- MongoDB Atlas cluster
- Conta no ThingSpeak com canal configurado

## ⚙️ Configuração

### 1. Instalação das Dependências

```bash
pip install -r requirements.txt
```

### 2. Variáveis de Ambiente

Crie um arquivo `.env` baseado no `.env.example`:

```env
# MongoDB Atlas
MONGO_URI=mongodb+srv://<username>:<password>@<cluster-url>/?retryWrites=true&w=majority&appName=cluster_name
MONGO_DB_NAME=NOME_DB
MONGO_COLLECTION_NAME=NOME_COLECAO

# ThingSpeak
THINGSPEAK_CHANNEL_ID=9999999
THINGSPEAK_READ_API_KEY=XXXXXXXXXXXXXXXX

# Servidor
HOST=0.0.0.0
PORT=8000
```

### 3. Configuração do ThingSpeak

Certifique-se de que seu canal no ThingSpeak tenha os fields configurados:
- `field1`: Umidade
- `field2`: Temperatura

## 🚀 Execução

### Desenvolvimento (com hot-reload)

```bash
python main.py
```

### Produção

```bash
uvicorn main:app --host 0.0.0.0 --port 8000
```

A API estará disponível em: `http://localhost:8000`

## 📚 Documentação da API

- **Swagger UI**: `http://localhost:8000/docs`
- **ReDoc**: `http://localhost:8000/redoc`

## 🔌 Endpoints Principais

### GET `/`
- **Descrição**: Health check da API
- **Resposta**: Status da aplicação

### POST `/data/fetch-and-save`
- **Descrição**: Dispara coleta manual de dados do ThingSpeak
- **Resposta**: 
```json
{
  "status": "sucesso",
  "message": "X registros foram salvos no MongoDB",
  "total_feeds_recebidos": 10
}
```

### GET `/data/all`
- **Descrição**: Retorna todos os dados salvos no MongoDB
- **Resposta**: Array de objetos `ThingSpeakData`

## ⚡ Funcionalidade de Agendamento

A aplicação inclui um **scheduler automático** que:

- ✅ Executa a cada **5 minutos** (configurável)
- ✅ Coleta dados do ThingSpeak automaticamente
- ✅ Salva no MongoDB com tratamento de duplicatas
- ✅ Logs detalhados no console

## 🛠️ Tecnologias Utilizadas

- **FastAPI** - Framework web moderno
- **MongoDB Atlas** - Banco de dados cloud
- **Pydantic** - Validação de dados
- **APScheduler** - Agendamento de tarefas
- **Requests** - Cliente HTTP
- **Uvicorn** - Servidor ASGI

## 📊 Estrutura de Dados

### Schema ThingSpeakData
```python
{
  "_id": "ObjectId",
  "created_at": "datetime",
  "field1": "float",  # Ex: Umidade
  "field2": "float",  # Ex: Temperatura
}
```

## 🔧 Customização

### Modificar Frequência de Coleta
No `main.py`, altere o intervalo no scheduler:

```python
# A cada 1 minuto
trigger=IntervalTrigger(minutes=1)

# A cada 30 segundos  
trigger=IntervalTrigger(seconds=30)

# A cada 1 hora
trigger=IntervalTrigger(hours=1)
```

### Adicionar Novos Fields
1. Atualize o schema em `entities/data_entity.py`
2. Modifique o mapeamento em `services/data_service.py`

## 🐛 Solução de Problemas

### Erro de Conexão MongoDB
- Verifique a `MONGO_URI` no `.env`
- Confirme as permissões do usuário no Atlas

### Erro ThingSpeak
- Valide `CHANNEL_ID` e `READ_API_KEY`
- Verifique se o canal é público ou a API key está correta

### Dados Não Aparecendo
- Confirme se há feeds no canal do ThingSpeak
- Verifique os logs do scheduler no console

---

**Desenvolvido com ❤️ usando FastAPI e MongoDB Atlas**

# D-SF — Arquitetura Ativa

## 1. Escopo deste documento

Este arquivo descreve **somente a arquitetura ativa e os contratos atualmente sustentados por evidência**, além das hipóteses futuras claramente separadas.

Histórico, benchmarks e motivos detalhados das decisões estão em `RESEARCH_LEDGER.md` e `VERIFICATION.md`.

Nenhuma seção futura deste arquivo deve misturar `VERIFIED` com hipótese não testada.

---

## 2. Estado arquitetural

| Área | Estado |
|---|---|
| World authority | `VERIFIED` — reference scope |
| Transactional mutation | `VERIFIED` — reference scope |
| Journal / replay / rollback | `VERIFIED` — same-architecture reference scope |
| Dependency-derived execution | `VERIFIED` — correctness scope |
| Hybrid scalar/range patches | `VERIFIED` — tested correctness/performance scope |
| Spatial Kernel | `NOT YET SELECTED` |
| Geometry Kernel | `NOT YET IMPLEMENTED` |
| GPU authoritative execution | `NOT VERIFIED` |
| Renderer production | `NOT IMPLEMENTED` |
| Physics production | `NOT IMPLEMENTED` |
| FOUNDATIONAL contracts | `NONE` |

---

## 3. Arquitetura verificada atual

```text
                            ┌─────────────────────────────┐
                            │ AUTHORITATIVE WORLD STATE   │
                            │ identity / state / tx cursor│
                            └──────────────┬──────────────┘
                                           │ immutable pre-wave view
                                           ▼
                            ┌─────────────────────────────┐
                            │ EXECUTION KERNEL            │
                            │ declared read/write sets    │
                            │ explicit dependencies       │
                            │ deterministic DAG / waves   │
                            └──────────────┬──────────────┘
                                           │
                          ┌────────────────┼────────────────┐
                          ▼                ▼                ▼
                    System A         System B         System N
                    private work     private work     private work
                          │                │                │
                          └────────────────┼────────────────┘
                                           ▼
                            ┌─────────────────────────────┐
                            │ PRIVATE PATCH OUTPUTS       │
                            │ scalar + typed ranges       │
                            └──────────────┬──────────────┘
                                           │ stable SystemId ordering
                                           ▼
                            ┌─────────────────────────────┐
                            │ HYBRID TRANSACTION          │
                            │ one TransactionId           │
                            │ full validation             │
                            └──────────────┬──────────────┘
                                           │
                                           ▼
                            ┌─────────────────────────────┐
                            │ PATCH JOURNAL / COMMIT      │
                            │ forward history             │
                            │ ephemeral undo              │
                            └──────────────┬──────────────┘
                                           │
                                           ▼
                            ┌─────────────────────────────┐
                            │ NEW AUTHORITATIVE WORLD     │
                            └──────────────┬──────────────┘
                                           │
                                           ▼
                            ┌─────────────────────────────┐
                            │ CANONICAL SHA-256           │
                            └─────────────────────────────┘
```

---

## 4. World Kernel — contratos verificados

### 4.1 World state é autoridade

O `World` de referência mantém o estado que os testes consideram autoritativo.

Renderer, física e neural representations não fazem parte do núcleo atual e não são necessários para reconstruir o estado testado.

### 4.2 Identidade

Propriedades testadas:

- Entity ID `0` reservado para mutações de escopo do mundo;
- IDs criados sequencialmente;
- identidade não depende de endereço de memória;
- criação de identidade ocorre dentro de `CreateEntity`;
- não existe reserva externa que avance secretamente o cursor de IDs;
- `next_entity_id()` é leitura no modelo corrigido.

### 4.3 Transaction ID

Transaction IDs são monotônicos no modelo de referência.

Uma transação com ID inválido é rejeitada.

### 4.4 Atomicidade de validação

A transação inteira é validada antes de a alteração autoritativa ser publicada.

Testes existentes verificam que uma operação inválida após operações válidas não produz commit parcial.

### 4.5 Estado numérico

Valores vetoriais não finitos (`NaN`/`Infinity`) são rejeitados nos caminhos testados.

`AdvanceReference` exige `dt` finito e não negativo.

---

## 5. Canonical State Hash

O hash de referência é SHA-256 sobre uma serialização canônica little-endian contendo, no modelo atual:

- schema version;
- next entity ID;
- last transaction ID;
- living entity count;
- IDs alocados em ordem;
- alive flag;
- health;
- bits IEEE-754 brutos de position e velocity.

Consequência deliberada:

```text
+0.0 != -0.0
```

para o hash, pois os bit patterns diferem.

O hash é um detector de **estado exato**, não de equivalência perceptual.

### Limitação ativa

O hash atual percorre o estado completo. Hash incremental/Merkle ainda é hipótese futura.

---

## 6. Journal e rollback

### 6.1 Persistência

O journal persistente armazena forward transactions.

Isso permite representar um frame de integração como:

```text
AdvanceReference(dt)
```

em vez de persistir todas as posições resultantes.

### 6.2 Undo

Rollback exato usa dados efêmeros de pre-state, separados do journal forward persistente.

Essa separação foi escolhida para não confundir:

- formato de história persistente;
- custo temporário de undo.

### 6.3 Guard de divergência

Antes de rollback, o journal compara o hash esperado com o hash atual do World.

Se o World divergiu fora da história controlada pelo journal, rollback é rejeitado em vez de aplicar undo potencialmente inválido.

### Limitações ativas

Ainda não resolvido:

- crash-safe record recovery;
- checksums por record;
- rollback storage comprimido/bounded;
- hashing incremental.

---

## 7. Execution Kernel

### 7.1 Sistema

Cada sistema possui:

- stable `SystemId`;
- read set;
- write set;
- optional explicit `after` dependencies;
- função de execução.

Recursos de referência usados em R2:

- `Identity`;
- `EntityState`;
- `Position`;
- `Velocity`;
- `Health`.

### 7.2 Access contract

O `SystemContext` fiscaliza acesso na implementação de referência.

Regras:

- `READ X + READ X` não cria hazard;
- `READ X + WRITE X` cria ordem;
- `WRITE X + READ X` cria ordem;
- `WRITE X + WRITE X` cria ordem;
- write permission não implica read permission;
- acesso não declarado rejeita o sistema antes do commit da wave.

### 7.3 DAG e waves

O builder deriva dependências a partir dos resource sets e das dependências explícitas.

Cycles são rejeitados.

Kahn topological ordering forma waves determinísticas.

Sistemas da mesma wave observam o mesmo pre-wave World.

### 7.4 Workers não têm autoridade

Workers geram private patches.

O World muda apenas depois que:

1. a wave termina;
2. outputs são ordenados canonicamente;
3. patches são fundidos;
4. a transação completa é validada;
5. ocorre publicação autoritativa.

### 7.5 Worker pool persistente

A primeira versão criava worker pool por `execute()`. Benchmarks mostraram que o lifecycle contaminava o tempo por frame.

O runtime atual possui `ExecutionRuntime` persistente.

A correção é parte da arquitetura ativa de R2.

---

## 8. Hybrid Transaction — R2.1

R2 demonstrou que `Mutation` por entidade podia dominar custo de materialização/merge/commit em updates densos.

R2.1 mantém uma única fronteira de autoridade e muda somente a granularidade da representação.

### 8.1 Lanes atuais

```text
PatchTransaction
├── scalar_mutations
├── vec3_patches
└── u32_patches
```

Uso atual:

- scalar: create/destroy, structure, scattered sparse writes;
- Vec3 ranges: Position / Velocity contiguous updates;
- U32 ranges: Health contiguous updates.

### 8.2 Mesmo TransactionId

Scalar e range não são dois sistemas de autoridade.

Ambos pertencem ao mesmo `PatchTransaction` e são publicados atomicamente.

### 8.3 No silent precedence

Se scalar e range tentarem escrever ambiguamente o mesmo componente/entidade dentro da mesma transação, a transação é rejeitada.

Não existe regra escondida “scalar ganha” ou “range ganha”.

### 8.4 Fixed pages não são semântica

R2.1 falsificou fixed page 256 como representação universal.

Portanto page size não faz parte da verdade do World.

Fixed pages ou copy-on-write podem voltar como otimização específica, mas precisam ser reavaliados por workload.

### 8.5 Execution integration

`SystemContext` suporta scalar e range writes.

O caminho legacy `execute()` rejeita range output.

`execute_patched()` realiza:

```text
DAG execution
→ private scalar/range outputs
→ stable SystemId merge
→ one hybrid PatchTransaction per wave
→ validate
→ publish
```

---

## 9. Determinismo — o que está e não está comprovado

### Comprovado no escopo atual

- replay exact-state em x86-64 Linux para os workloads registrados;
- GCC 14.2 e Clang 17 produziram hashes idênticos nos cenários registrados;
- worker counts diferentes produziram hash idêntico nos testes de R2;
- serial oracle e caminhos R2.1 produziram hash idêntico nos workloads registrados.

### Não comprovado

- Windows ↔ Linux;
- x86-64 ↔ ARM;
- diferentes políticas/FPU modes;
- CPU ↔ GPU;
- todos os futuros sistemas;
- rede distribuída.

Nenhum documento deve chamar o estado atual de “determinismo universal”.

---

## 10. Arquitetura futura — hipóteses, não contratos

### R3 — Spatial Kernel

Objetivo: testar structures sob uma interface comum sem escolher vencedora antecipadamente.

```text
World spatial data
    ↓
SpatialBackend contract
    ├── flat grid
    ├── hash grid
    ├── BVH
    ├── sparse brick hierarchy
    └── octree
```

### R4 — Geometry Kernel

Hipótese:

```text
World Object / Region
    ↓
GeometryProvider
    ├── Triangle
    ├── SDF
    ├── Voxel
    ├── Gaussian/Splat
    └── future provider
```

Ainda não implementado.

### R5 — GPU Laboratory

Hipótese de execução futura:

```text
Execution Graph
    ├── CPU workers
    ├── GPU compute
    └── future accelerators
```

A seleção deve depender de custo, dependências e hardware; não de uma regra “GPU first” absoluta.

### R6 — Heterogeneous World

Hipótese mais ambiciosa:

```text
Authoritative object/region
    ├── Render representation A
    ├── Physics representation B
    ├── destruction representation C
    └── distant representation D
```

As representações são views e podem diferir sem mudar a identidade autoritativa.

Ainda não comprovado.

---

## 11. Candidatos a contrato futuro

Estes itens sobreviveram aos testes até R2.1, mas **ainda não são FOUNDATIONAL**:

1. World state é autoridade; derived views não devem ser autoridade.
2. Identidade não deve depender de memória/renderer/scene graph.
3. Mudança autoritativa deve cruzar uma fronteira validada.
4. Optimized path deve ser comparável a um oracle/reference.
5. Workers calculam propostas; publicação autoritativa é separada.
6. Declared access deve ser machine-checkable.
7. Granularidade de patch é adaptativa e não semântica.
8. Overlap ambíguo não deve ser resolvido por precedência silenciosa.

Qualquer promoção futura precisa cumprir os critérios de `PROJECT.md`.

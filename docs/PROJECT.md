# D-SF — Projeto, Governança e Critérios de Fechamento

## 1. Propósito deste documento

Este arquivo é a fonte canônica para:

- intenção original do projeto;
- problema que o laboratório tenta resolver;
- escopo e antiobjetivos;
- método de pesquisa;
- regras de evidência e promoção;
- roadmap ativo;
- definição de “feito” para cada etapa e para o programa de pesquisa;
- governança documental e de alterações.

Ele deve ser atualizado apenas quando uma decisão de projeto for explicitamente tomada ou quando uma evidência comprovada alterar o plano. Resultados experimentais detalhados pertencem ao `RESEARCH_LEDGER.md` e ao `VERIFICATION.md`; a arquitetura ativa pertence ao `ARCHITECTURE.md`.

---

## 2. Intenção de origem

O projeto nasceu de uma pergunta mais ambiciosa do que “como criar outro renderer?”: se fosse possível pesquisar livremente, testar hipóteses e rejeitar abordagens que falhassem, como projetar um motor que pudesse redesenhar partes importantes da arquitetura tradicional de engines?

A intenção inicial explicitada na conversa foi:

1. começar pelo núcleo, não pelo editor ou por uma demonstração visual;
2. dividir responsabilidades em núcleos independentes quando isso fosse comprovadamente vantajoso;
3. testar teorias em laboratório antes de promovê-las;
4. tornar estáveis apenas contratos que sobrevivessem aos testes, e não congelar implementações cedo;
5. pensar no projeto como um sistema inteiro, evitando otimizações locais que piorassem a arquitetura global;
6. documentar resultados, falhas e razões de decisão com qualidade de auditoria;
7. explorar não apenas o que já é convencional, mas também arquiteturas plausíveis que ainda precisem ser demonstradas;
8. nunca transformar uma hipótese atraente em fato sem evidência.

A formulação central que emergiu da conversa foi:

> **O mundo não deve depender de polígonos, renderer, física ou processador específico. Essas tecnologias devem poder ser representações ou views derivadas de um estado autoritativo mais fundamental.**

Isso permanece uma direção de pesquisa; apenas as partes explicitamente marcadas como `VERIFIED` foram demonstradas no laboratório atual.

---

## 3. Missão

Descobrir, por experimentos reproduzíveis, uma arquitetura de engine que maximize:

- complexidade de mundo;
- qualidade perceptual;
- previsibilidade e auditabilidade do estado;
- capacidade de paralelização;
- liberdade de trocar representações e backends;

por unidade de:

- tempo de CPU/GPU;
- memória;
- largura de banda;
- latência;
- complexidade de produção e manutenção.

Não existe uma única métrica chamada “eficiência”. Toda afirmação de melhoria deve dizer **qual custo diminuiu, em qual workload e sob quais condições**.

---

## 4. Problema arquitetural investigado

Engines tradicionais frequentemente acoplam partes do mundo a conceitos como:

- scene graph;
- mesh como representação dominante;
- objetos que atualizam estado diretamente;
- frame phases fixas;
- separação rígida CPU-engine / GPU-renderer;
- LOD e representação escolhidos pelo conteúdo, não pelo orçamento ou erro permitido;
- sistemas de replay, rollback, networking, editor undo e persistência implementados separadamente.

D-SF investiga se uma arquitetura diferente pode ser superior em workloads relevantes:

```text
Authoritative World State
        ↓
Derived execution / spatial / geometry views
        ↓
CPU / GPU / future accelerators
        ↓
Render / Physics / Audio / AI representations
```

A geometria visual deve ser um meio de representar o mundo, não sua identidade.

---

## 5. Hipóteses de alto nível ainda abertas

Estas hipóteses são deliberadamente registradas como **não comprovadas**:

### H-A — Representation independence
Mesh, SDF, voxels, Gaussian splats e futuras representações neurais podem coexistir como providers sem que nenhuma defina a identidade do objeto.

### H-B — Execution independence
Sistemas podem declarar dependências e restrições enquanto o kernel decide onde e como executar, permitindo CPU, GPU e futuros aceleradores como workers especializados quando apropriado.

### H-C — Transaction-derived history
Replay, rollback, networking, editor undo e persistência incremental podem compartilhar um modelo comum de mudanças autoritativas.

R1 comprovou somente replay/rollback no escopo de referência. Networking, editor undo e persistência de produção ainda não foram comprovados.

### H-D — Adaptive representation by error/budget
A engine pode futuramente escolher representação e qualidade por orçamento/erro permitido em vez de depender apenas de LODs pré-fixados.

Nenhum sistema de “error budget” foi implementado até R2.1.

### H-E — Heterogeneous world
Uma mesma região pode usar representações diferentes para render, física, destruição, distância ou captura, mantendo uma identidade autoritativa comum.

Ainda não demonstrado.

---

## 6. Princípios de pesquisa

### 6.1 Referência antes de otimização

Toda otimização importante deve ter um oracle simples contra o qual possa ser comparada.

Se o caminho otimizado divergir do oracle quando deveria ser semanticamente idêntico, o caminho otimizado falhou mesmo que seja mais rápido.

### 6.2 Correção antes de performance

A ordem padrão é:

```text
correctness
→ reproducibility
→ adversarial tests
→ measurement
→ optimization
→ promotion
```

### 6.3 Dados podem matar uma ideia

Nenhuma tecnologia é favorita. Se uma hipótese perder no workload para o qual foi proposta, ela pode ser rejeitada, limitada a outro domínio ou mantida somente como experimento.

### 6.4 Não fabricar evidência de hardware

CPU benchmark não é GPU benchmark.

Estimativa teórica não é medição.

A sandbox atual não fornece base para reivindicar desempenho real de Vulkan, DirectX 12, RT cores, Tensor cores ou VRAM.

### 6.5 Contratos podem estabilizar; algoritmos continuam substituíveis

Uma implementação que vence hoje não se torna imutável.

O que pode futuramente chegar a `FOUNDATIONAL` é um contrato/invariante versionado.

### 6.6 Falhas e regressões são resultados

Uma regressão encontrada não deve ser apagada do histórico. Ela deve ser registrada junto da correção e do motivo.

### 6.7 Não otimizar propaganda

Benchmarks de entidades mínimas não podem ser apresentados como “NPCs completos”.

Testes sintéticos devem ser nomeados exatamente pelo trabalho que executam.

---

## 7. Estados de promoção

### `IDEA`
Possibilidade ainda não convertida em proposição falsificável.

### `HYPOTHESIS`
Proposição clara, com condição de sucesso e falha planejada.

### `EXPERIMENTAL`
Existe código ou protótipo, mas pode mudar sem garantia de compatibilidade.

### `VERIFIED`
Passou pelo conjunto de verificações definido e tem escopo explícito de validade.

Cada uso de `VERIFIED` deve indicar o escopo, por exemplo:

```text
VERIFIED — x86-64 Linux reference scope
```

### `FOUNDATIONAL`
Contrato suficientemente estável para outras camadas dependerem dele como fundação.

Para chegar a `FOUNDATIONAL`, no mínimo será exigido:

- comportamento especificado;
- oracle/reference path quando aplicável;
- testes de regressão permanentes;
- testes adversariais relevantes;
- evidência em mais de um compilador;
- evidência multiplataforma quando o contrato for multiplataforma;
- análise de limitações conhecidas;
- decisão formal registrada no ledger;
- nenhum requisito crítico aberto que contradiga o contrato.

Até R2.1: **nenhum contrato é FOUNDATIONAL**.

---

## 8. Regra de atualização documental

### 8.1 Informação comprovada

Uma afirmação pode entrar na seção “estado atual” somente se for sustentada por pelo menos uma das seguintes evidências:

- teste automatizado reproduzível;
- benchmark com workload e ambiente registrados;
- hash/replay reproduzível;
- inspeção direta do código atual;
- resultado de compilação/sanitizer registrado;
- decisão explícita de governança/projeto.

### 8.2 Informação não comprovada

Pode ser registrada somente como:

- `IDEA`;
- `HYPOTHESIS`;
- `OPEN`;
- `NOT VERIFIED`;
- `DEFERRED`.

Nunca deve aparecer misturada a resultados comprovados.

### 8.3 Toda decisão deve registrar “por quê”

Uma decisão arquitetural deve conter:

- problema;
- alternativas consideradas;
- evidência disponível;
- decisão;
- razão;
- limitações;
- condição que justificaria revisá-la.

### 8.4 Não apagar evidência contraditória

Quando um resultado é superado:

1. manter o resultado antigo no ledger;
2. registrar o novo experimento;
3. explicar por que a decisão mudou;
4. atualizar somente o estado ativo em `ARCHITECTURE.md` e `README.md`.

---

## 9. Governança dos arquivos

### Regra de fonte única

Não criar um novo relatório apenas para repetir informação que já pertence a um documento canônico.

Matriz de responsabilidade:

| Informação | Arquivo canônico |
|---|---|
| Missão, escopo, regras, roadmap, fechamento | `docs/PROJECT.md` |
| Arquitetura ativa e contratos | `docs/ARCHITECTURE.md` |
| História e decisões em ordem temporal | `docs/RESEARCH_LEDGER.md` |
| Evidência, hashes, ambientes, comandos | `docs/VERIFICATION.md` |
| Navegação e estado resumido | `README.md` |

### Regra de atualização coordenada

Quando um experimento promove ou rejeita uma hipótese:

1. adicionar entrada detalhada ao `RESEARCH_LEDGER.md`;
2. adicionar/atualizar evidência no `VERIFICATION.md`;
3. atualizar `ARCHITECTURE.md` somente se a arquitetura ativa mudou;
4. atualizar `PROJECT.md` somente se roadmap/regra/escopo mudou;
5. atualizar `README.md` para refletir o estágio ativo.

### Regra de código

Código experimental pode mudar durante uma fase, mas uma fase marcada `VERIFIED` deve possuir testes permanentes que preservem a propriedade demonstrada.

---

## 10. Política de repositório

Este repositório `AiltonSantanaReis/D-SF` é a fonte oficial deste projeto.

A partir do baseline inicial:

- trabalho futuro deve ser feito e registrado neste repositório;
- outros repositórios não devem ser usados como fonte de verdade para D-SF;
- nenhum resultado externo deve ser importado silenciosamente como se tivesse sido produzido aqui;
- quando referências públicas forem usadas em pesquisa futura, elas devem ser registradas como referências, separadas da evidência produzida pelo D-SF;
- `main` deve representar somente estados coerentes e testados;
- alterações experimentais relevantes devem preferencialmente ser desenvolvidas em branch e integradas após a verificação definida para a fase.

---

## 11. Roadmap ativo

### R0 — Minimal Authoritative World — `VERIFIED`

Pergunta:

> Qual é o menor modelo de estado que pode permanecer válido enquanto renderer, física e backend são substituídos?

Comprovado no escopo de referência:

- identidade estável e sequencial;
- alocação de identidade transacional;
- transações totalmente validadas antes da mutação;
- rejeição de valores vetoriais não finitos;
- avanço de simulação como mutação autoritativa;
- baseline de 1 milhão de entidades leves.

### R1 — Change Journal / Hash / Replay / Rollback — `VERIFIED`

Pergunta:

> O estado pode ser reconstruído apenas pelas transações persistidas e voltar exatamente a estados históricos?

Comprovado no escopo atual:

- journal forward-only;
- SHA-256 canônico;
- replay a partir de `World` novo;
- rollback exato por undo efêmero;
- proteção contra divergência fora do journal;
- save/load binário;
- igualdade de hash GCC/Clang no x86-64 Linux testado.

### R2 — Dependency Execution Graph — `VERIFIED correctness / PARTIAL performance`

Pergunta:

> Dependências declaradas podem gerar paralelismo seguro sem dar autoridade direta aos workers?

Comprovado:

- read/read compartilha wave;
- read/write e write/write serializam;
- cycles são rejeitados;
- acessos não declarados falham antes do commit;
- serial e worker pool chegam ao mesmo hash;
- worker count 1/2/4/5 não altera o resultado no cenário testado;
- worker pool persistente evita custo de criação por frame.

Resultado de performance:

- scheduler escala em tarefas computacionais independentes;
- patches por entidade limitaram o ganho do world workload;
- performance de R2 ficou `PARTIAL`.

### R2.1 — Hybrid Transaction Patches — `VERIFIED`

Pergunta:

> É possível preservar R0/R1/R2 sem representar cada alteração densa por uma `Mutation` individual?

Comprovado:

- uma transação pode combinar scalar writes e typed contiguous ranges;
- mesma autoridade, mesmo `TransactionId`;
- overlap ambíguo é rejeitado;
- replay/rollback/save/load continuam bit-exatos nos testes;
- fixed page 256 não venceu universalmente;
- workloads esparsos podem favorecer scalar;
- workloads densos podem favorecer ranges fortemente;
- Execution Kernel pode produzir ranges através de `execute_patched()`.

### R3 — Spatial Kernel Bake-Off — `NEXT / HYPOTHESIS`

Nenhuma estrutura foi escolhida.

Candidatos mínimos planejados:

- flat/uniform grid;
- hash grid;
- BVH;
- sparse brick hierarchy;
- octree;
- hierarchical hash grid, se a implementação de referência justificar inclusão.

Workloads mínimos planejados:

- mundo estático denso;
- mundo estático esparso;
- objetos móveis;
- teleports;
- near/range queries;
- ray queries;
- large-area queries;
- updates em escala;
- streaming regional;
- modificação/destruição regional.

Métricas mínimas:

- build time;
- update time;
- query time;
- memória;
- número de alocações/estrutura auxiliar quando relevante;
- escalabilidade por volume/densidade;
- estabilidade do resultado;
- custo de integração com a fronteira transacional.

### R4 — Geometry Kernel — `PLANNED / NOT VERIFIED`

Objetivo de pesquisa:

- contrato de `GeometryProvider`;
- mesh, SDF, voxel e outras representações sob uma identidade comum;
- conversões/proxies somente quando comprovadamente úteis;
- separar geometria visual de geometria física.

### R5 — GPU Laboratory — `PLANNED / NOT VERIFIED`

Somente hardware real pode produzir evidência desta etapa.

Possíveis pesquisas:

- Vulkan/DX12 compute;
- GPU resident data;
- indirect execution;
- GPU patches;
- SDF raymarching;
- sparse residency;
- Gaussian splat renderer;
- timestamps e bandwidth reais.

### R6 — Heterogeneous World — `PLANNED / NOT VERIFIED`

Objetivo:

- integrar Spatial + Geometry + Execution;
- permitir representações diferentes como views derivadas do mesmo objeto/região;
- testar mudança de representação sem perder identidade/estado autoritativo;
- produzir demonstração integrada e benchmarks comparáveis.

---

## 12. Critério de fechamento de cada fase

Uma fase de pesquisa só fecha quando:

1. hipótese está escrita em termos falsificáveis;
2. reference/oracle está definido quando necessário;
3. candidatos são comparados sob o mesmo contrato;
4. correctness tests passam;
5. adversarial/negative tests relevantes passam;
6. resultados e ambiente são registrados;
7. limitações são explicitadas;
8. decisão é registrada com motivo;
9. documentação canônica é atualizada sem duplicação;
10. pacote/repositório limpo reconstrói o resultado relevante.

“Funcionou uma vez” não fecha uma fase.

---

## 13. Critério de fechamento do programa de pesquisa

O programa de pesquisa inicial D-SF pode ser considerado encerrado e pronto para uma fase de productization somente quando existir evidência de que os objetivos arquiteturais centrais foram testados de ponta a ponta.

### Research Closure Gate — RC-1

RC-1 exige, no mínimo:

1. **World authority:** contratos essenciais de identidade, estado e transação promovidos ou explicitamente rejeitados/substituídos.
2. **History:** estratégia escalável para replay/rollback/hash com limites conhecidos.
3. **Execution:** scheduler demonstrado em workloads reais com política de serial/paralelo adaptativa.
4. **Space:** estrutura ou política espacial escolhida por dados, podendo ser híbrida se os dados exigirem.
5. **Geometry:** ao menos duas representações geométricas distintas coexistindo sob o mesmo contrato de identidade.
6. **Derived views:** renderer e física demonstrando que não são autoridade sobre o `World`.
7. **GPU evidence:** pelo menos um backend GPU medido em hardware real; nenhuma dependência de estimativas fictícias.
8. **Cross-platform:** conjunto mínimo de testes em Windows/Linux e, se o contrato declarar independência de arquitetura, validação adicional em outra arquitetura antes da promoção correspondente.
9. **Integrated demonstrator:** cenário reproduzível que exercite state → execution → space → geometry → derived view.
10. **Comparative baseline:** cada reivindicação de superioridade deve estar ligada a um baseline e workload definidos.
11. **Auditability:** ledger, verificação e arquitetura devem explicar como cada contrato atual foi obtido.
12. **No critical unknown:** nenhum problema conhecido capaz de invalidar o núcleo pode estar sendo ocultado sob status `VERIFIED`/`FOUNDATIONAL`.

RC-1 não exige que D-SF “vença todas as engines em tudo”; isso seria uma meta tecnicamente inválida. Exige que o projeto possa demonstrar claramente **onde, por que e sob quais workloads sua arquitetura oferece vantagem**, e onde ela não oferece.

---

## 14. Antiobjetivos permanentes

- substituir triângulos apenas por novidade;
- tornar “GPU-only” uma ideologia;
- chamar SDF, voxel, Gaussian ou neural representation de solução universal antes de testes;
- congelar um algoritmo porque venceu um único benchmark;
- esconder regressões;
- confundir qualidade visual com estado físico correto;
- usar IA generativa como autoridade do mundo sem validação determinística;
- construir editor completo antes de a arquitetura central justificar esse investimento;
- produzir números de hardware não medidos;
- proliferar documentos de estado concorrentes.

---

## 15. Próxima ação autorizada

**R3 — Spatial Kernel Bake-Off.**

Antes de qualquer promoção, o R3 deve criar uma interface comum, oracle/validação de query e benchmarks comparáveis. A escolha de octree, BVH, grid, sparse bricks ou combinação permanece aberta até que os dados existam.

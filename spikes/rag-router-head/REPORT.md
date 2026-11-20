# Router v2 head — training report

Дата: 2026-11-18. Энкодер: embeddinggemma-2-270m (LiteRT int8, CPU),
plain-текст реплики без recipe-префикса. Данные: router_ds_draft (84 синтетические
реплики, анонимизированная копия в test/resources/rag/router_ds.tsv).
Модель: multinomial logistic regression (4 класса), full-batch GD 3000 итераций,
lr 0.5, L2 1e-3. Веса: src/rag/router_head_data.h.

| Метрика | Значение |
|---|---|
| train accuracy (4 класса) | 1.000 |
| leave-one-out accuracy | 0.881 |
| binary search precision | 1.000 |
| binary search recall | 1.000 |

Оговорка: датасет синтетический и маленький, автор правил = автор датасета;
LOO — честная оценка на этих данных. Переобучить на реальных транскриптах
до продакшена (handoff 17.11 §2). Голова включается настройкой, дефолт — правила.

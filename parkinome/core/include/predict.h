#ifndef PREDICT_H
#define PREDICT_H

/* ===== КОДЫ СТАТУСА ===== */
#define PARKINOME_OK 0
#define PARKINOME_ERR_INVALID_INPUT 1
#define PARKINOME_INVALID_RANGE 2
#define PARKINOME_NAN_DETECTED 3
#define PARKINOME_NULL_POINTER 4

/* ===== ВХОДНЫЕ ДАННЫЕ ===== */
typedef struct {

    /* идентификатор пациента */
    char patient_id[64]; int has_patient_id;

    /* клинические */
    double age; int has_age;
    double updrs_iii; int has_updrs_iii;
    double scopa_aut; int has_scopa_aut;
    double hoehn_yahr; int has_hoehn_yahr;

    /* когнитивные */
    double moca; int has_moca;

    /* биомаркеры */
    double ndufa4l2; int has_ndufa4l2;
    double ndufs2; int has_ndufs2;
    double pink1; int has_pink1;
    double ppargc1a; int has_ppargc1a;
    double nlrp3; int has_nlrp3;
    double il1b; int has_il1b;
    double s100a8; int has_s100a8;
    double cxcl8; int has_cxcl8;

} parkinome_input_t;


/* ===== ВЫХОДНЫЕ ДАННЫЕ ===== */
typedef struct {

    double isp;
    double risk_probability;
    int category;
    double confidence;
    /* Биологически интерпретируемые промежуточные индексы (z-space aggregates). */
    double mito_score;    /* Средний z-score митохондриальной группы */
    double inflam_score;  /* Средний z-score воспалительной группы */
    double imbalance;     /* inflam_score - mito_score */

    /* Линейные вклады групп в raw-logit (могут быть <0 или >0). */
    double breakdown_clinical;
    double breakdown_cognitive;
    double breakdown_inflammation;
    double breakdown_mitochondrial;
    double breakdown_imbalance;

    /* Нормализованные индексы [0..1] для UI-визуализации. */
    double mito_index;
    double inflam_index;
    double stress_index;

    /* Human-readable уровни как enum:
       0 = LOW, 1 = INTERMEDIATE, 2 = HIGH. */
    int mito_level;
    int inflam_level;

} parkinome_output_t;


/* ===== ПУБЛИЧНЫЙ ИНТЕРФЕЙС ===== */
int parkinome_predict(parkinome_input_t *in, parkinome_output_t *out);

#endif

#ifndef PREDICT_H
#define PREDICT_H

/* ===== STATUS ===== */
#define PARKINOME_OK 0
#define PARKINOME_ERR_INVALID_INPUT 1
#define PARKINOME_INVALID_RANGE 2
#define PARKINOME_NAN_DETECTED 3
#define PARKINOME_NULL_POINTER 4

/* ===== INPUT ===== */
typedef struct {

    /* clinical */
    double age; int has_age;
    double updrs_iii; int has_updrs_iii;
    double scopa_aut; int has_scopa_aut;
    double hoehn_yahr; int has_hoehn_yahr;

    /* cognitive */
    double moca; int has_moca;

    /* biomarkers */
    double ndufa4l2; int has_ndufa4l2;
    double ndufs2; int has_ndufs2;
    double pink1; int has_pink1;
    double ppargc1a; int has_ppargc1a;
    double nlrp3; int has_nlrp3;
    double il1b; int has_il1b;
    double s100a8; int has_s100a8;
    double cxcl8; int has_cxcl8;

} parkinome_input_t;


/* ===== OUTPUT ===== */
typedef struct {

    double isp;
    double risk_probability;
    int category;
    double confidence;

} parkinome_output_t;


/* ===== API ===== */
int parkinome_predict(parkinome_input_t *in, parkinome_output_t *out);

#endif
#include <stdio.h>
#include <stdlib.h>

#include "json_io.h"
#include "train.h"
#include "model.h"

int main(int argc, char **argv) {
    const char *dataset_path = "data/train_example.json";
    char *json = NULL;
    training_result_t result;
    train_config_t cfg;

    if (argc > 1) dataset_path = argv[1];

    json = read_file(dataset_path);
    if (!json) {
        fprintf(stderr, "Cannot read dataset: %s\n", dataset_path);
        return 1;
    }

    train_default_config(&cfg);
    cfg.epochs = 1500;
    cfg.learning_rate = 0.01;
    cfg.l2 = 0.001;

    if (train_and_save_model_from_json(json, &cfg, PARKINOME_MODEL_FILE, &result) != 0) {
        free(json);
        fprintf(stderr, "Training failed\n");
        return 1;
    }

    free(json);
    printf("Training complete\n");
    printf("accuracy=%.4f precision=%.4f recall=%.4f specificity=%.4f\n",
           result.accuracy, result.precision, result.recall, result.specificity);
    printf("samples=%d train=%d test=%d intercept=%.6f\n",
           result.samples, result.train_size, result.test_size, result.intercept);
    return 0;
}

zip -r flexbox_euis.zip flexbox_euis/ && \
aws s3 cp flexbox_euis.zip s3://flexbox-euis-701424605739-us-east-1-an/flexbox_euis.zip && \
aws s3 presign s3://flexbox-euis-701424605739-us-east-1-an/flexbox_euis.zip --expires-in 604800
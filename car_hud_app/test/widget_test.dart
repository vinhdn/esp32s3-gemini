import 'package:flutter_test/flutter_test.dart';

import 'package:car_hud_app/main.dart';

void main() {
  testWidgets('Car HUD app hien thi man hinh chinh', (WidgetTester tester) async {
    await tester.pumpWidget(const CarHudApp());

    expect(find.text('Car HUD'), findsOneWidget);
    expect(find.text('Quét & Kết nối tới board'), findsOneWidget);
  });
}
